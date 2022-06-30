#include "linne_encoder.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "linne_lpc_predict.h"
#include "linne_internal.h"
#include "linne_utility.h"
#include "byte_array.h"
#include "bit_stream.h"
#include "lpc.h"
#include "linne_network.h"
#include "linne_coder.h"
#include "static_huffman.h"

/* エンコーダハンドル */
struct LINNEEncoder {
    struct LINNEHeader header; /* ヘッダ */
    struct LINNECoder *coder; /* 符号化ハンドル */
    uint32_t max_num_channels; /* バッファチャンネル数 */
    uint32_t max_num_samples_per_block; /* バッファサンプル数 */
    uint32_t max_num_layers; /* 最大レイヤー数 */
    uint32_t max_num_parameters_per_layer; /* 最大レイヤーあたりパラメータ数 */
    uint8_t set_parameter; /* パラメータセット済み？ */
    uint8_t enable_learning; /* ネットワークの学習を行う？ */
    uint8_t num_afmethod_iterations; /* 補助関数法の繰り返し回数(0で実行しない) */
    struct LINNEPreemphasisFilter **pre_emphasis; /* プリエンファシスフィルタ */
    int32_t **pre_emphasis_prev; /* プリエンファシスフィルタの直前のサンプル */
    struct LINNENetwork *network; /* ネットワーク */
    struct LINNENetworkTrainer *trainer; /* LPCネットワークトレーナー */
    double ***params_double; /* LPC係数(double) */
    int32_t ***params_int; /* LPC係数(int) */
    uint32_t **num_units; /* 各層のユニット数 */
    uint32_t **rshifts; /* 各層のLPC係数右シフト量 */
    int32_t **buffer_int; /* 信号バッファ(int) */
    int32_t **residual; /* 残差信号 */
    double *buffer_double; /* 信号バッファ(double) */
    const struct LINNEParameterPreset *parameter_preset; /* パラメータプリセット */
    struct StaticHuffmanCodes coef_code; /* 係数ハフマン符号 */
    uint8_t alloced_by_own; /* 領域を自前確保しているか？ */
    void *work; /* ワーク領域先頭ポインタ */
};

/* エンコードパラメータをヘッダに変換 */
static LINNEError LINNEEncoder_ConvertParameterToHeader(
        const struct LINNEEncodeParameter *parameter, uint32_t num_samples,
        struct LINNEHeader *header);
/* ブロックデータタイプの判定 */
static LINNEBlockDataType LINNEEncoder_DecideBlockDataType(
        struct LINNEEncoder *encoder, const int32_t *const *input, uint32_t num_samples);

/* ヘッダエンコード */
LINNEApiResult LINNEEncoder_EncodeHeader(
        const struct LINNEHeader *header, uint8_t *data, uint32_t data_size)
{
    uint8_t *data_pos;

    /* 引数チェック */
    if ((header == NULL) || (data == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }

    /* 出力先バッファサイズ不足 */
    if (data_size < LINNE_HEADER_SIZE) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* ヘッダ異常値のチェック */
    /* データに書き出す（副作用）前にできる限りのチェックを行う */
    /* チャンネル数 */
    if (header->num_channels == 0) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* サンプル数 */
    if (header->num_samples == 0) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* サンプリングレート */
    if (header->sampling_rate == 0) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* ビット深度 */
    if (header->bits_per_sample == 0) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* ブロックあたりサンプル数 */
    if (header->num_samples_per_block == 0) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* パラメータプリセット */
    if (header->preset >= LINNE_NUM_PARAMETER_PRESETS) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* マルチチャンネル処理法 */
    if (header->ch_process_method >= LINNE_CH_PROCESS_METHOD_INVALID) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* モノラルではMS処理はできない */
    if ((header->ch_process_method == LINNE_CH_PROCESS_METHOD_MS)
            && (header->num_channels == 1)) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }

    /* 書き出し用ポインタ設定 */
    data_pos = data;

    /* シグネチャ */
    ByteArray_PutUint8(data_pos, 'I');
    ByteArray_PutUint8(data_pos, 'B');
    ByteArray_PutUint8(data_pos, 'R');
    ByteArray_PutUint8(data_pos, 'A');
    /* フォーマットバージョン
    * 補足）ヘッダの設定値は無視してマクロ値を書き込む */
    ByteArray_PutUint32BE(data_pos, LINNE_FORMAT_VERSION);
    /* コーデックバージョン
    * 補足）ヘッダの設定値は無視してマクロ値を書き込む */
    ByteArray_PutUint32BE(data_pos, LINNE_CODEC_VERSION);
    /* チャンネル数 */
    ByteArray_PutUint16BE(data_pos, header->num_channels);
    /* サンプル数 */
    ByteArray_PutUint32BE(data_pos, header->num_samples);
    /* サンプリングレート */
    ByteArray_PutUint32BE(data_pos, header->sampling_rate);
    /* サンプルあたりビット数 */
    ByteArray_PutUint16BE(data_pos, header->bits_per_sample);
    /* 最大ブロックあたりサンプル数 */
    ByteArray_PutUint32BE(data_pos, header->num_samples_per_block);
    /* パラメータプリセット */
    ByteArray_PutUint8(data_pos, header->preset);
    /* マルチチャンネル処理法 */
    ByteArray_PutUint8(data_pos, header->ch_process_method);

    /* ヘッダサイズチェック */
    LINNE_ASSERT((data_pos - data) == LINNE_HEADER_SIZE);

    /* 成功終了 */
    return LINNE_APIRESULT_OK;
}

/* エンコードパラメータをヘッダに変換 */
static LINNEError LINNEEncoder_ConvertParameterToHeader(
        const struct LINNEEncodeParameter *parameter, uint32_t num_samples,
        struct LINNEHeader *header)
{
    struct LINNEHeader tmp_header = { 0, };

    /* 引数チェック */
    if ((parameter == NULL) || (header == NULL)) {
        return LINNE_ERROR_INVALID_ARGUMENT;
    }

    /* パラメータのチェック */
    if (parameter->num_channels == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    if (parameter->bits_per_sample == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    if (parameter->sampling_rate == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    if (parameter->num_samples_per_block == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    if (parameter->preset >= LINNE_NUM_PARAMETER_PRESETS) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    if (parameter->ch_process_method >= LINNE_CH_PROCESS_METHOD_INVALID) {
        return LINNE_ERROR_INVALID_FORMAT;
    }

    /* プリセットのパラメータ数がブロックサイズを超えていないかチェック */
    {
        uint32_t l;
        const struct LINNEParameterPreset *preset = &g_linne_parameter_preset[parameter->preset];
        for (l = 0; l < preset->num_layers; l++) {
            /* 1サンプル遅れの畳込みを行うため、サンプル数はパラメータ数よりも大きいことを要求 */
            if (parameter->num_samples_per_block <= preset->layer_num_params_list[l]) {
                return LINNE_ERROR_INVALID_FORMAT;
            }
        }
    }

    /* 総サンプル数 */
    tmp_header.num_samples = num_samples;

    /* 対応するメンバをコピー */
    tmp_header.num_channels = parameter->num_channels;
    tmp_header.sampling_rate = parameter->sampling_rate;
    tmp_header.bits_per_sample = parameter->bits_per_sample;
    tmp_header.num_samples_per_block = parameter->num_samples_per_block;
    tmp_header.preset = parameter->preset;
    tmp_header.ch_process_method = parameter->ch_process_method;

    /* 成功終了 */
    (*header) = tmp_header;
    return LINNE_ERROR_OK;
}

/* エンコーダハンドル作成に必要なワークサイズ計算 */
int32_t LINNEEncoder_CalculateWorkSize(const struct LINNEEncoderConfig *config)
{
    int32_t work_size, tmp_work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if ((config->max_num_samples_per_block == 0)
            || (config->max_num_channels == 0)
            || (config->max_num_layers == 0)
            || (config->max_num_parameters_per_layer == 0)) {
        return -1;
    }

    /* ブロックサイズはパラメータ数より大きくなるべき */
    if (config->max_num_parameters_per_layer > config->max_num_samples_per_block) {
        return -1;
    }

    /* ハンドル本体のサイズ */
    work_size = sizeof(struct LINNEEncoder) + LINNE_MEMORY_ALIGNMENT;

    /* 符号化ハンドルのサイズ */
    if ((tmp_work_size = LINNECoder_CalculateWorkSize()) < 0) {
        return -1;
    }
    work_size += tmp_work_size;

    /* LPCネットのサイズ */
    if ((tmp_work_size = LINNENetwork_CalculateWorkSize(
                    config->max_num_samples_per_block, config->max_num_layers, config->max_num_parameters_per_layer)) < 0) {
        return -1;
    }
    work_size += tmp_work_size;

    /* トレーナーのサイズ */
    if ((tmp_work_size = LINNENetworkTrainer_CalculateWorkSize(
                    config->max_num_layers, config->max_num_parameters_per_layer)) < 0) {
        return -1;
    }
    work_size += tmp_work_size;

    /* プリエンファシスフィルタのサイズ */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(struct LINNEPreemphasisFilter, config->max_num_channels, LINNE_NUM_PREEMPHASIS_FILTERS);
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, LINNE_NUM_PREEMPHASIS_FILTERS);
    /* パラメータバッファ領域 */
    /* LPC係数(int) */
    work_size += LINNE_CALCULATE_3DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_layers, config->max_num_parameters_per_layer);
    /* LPC係数(double) */
    work_size += LINNE_CALCULATE_3DIMARRAY_WORKSIZE(double, config->max_num_channels, config->max_num_layers, config->max_num_parameters_per_layer);
    /* 各層のユニット数 */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(uint32_t, config->max_num_channels, config->max_num_layers);
    /* 各層のLPC係数右シフト量 */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(uint32_t, config->max_num_channels, config->max_num_layers);
    /* 信号処理バッファのサイズ */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_samples_per_block);
    work_size += config->max_num_samples_per_block * sizeof(double) + LINNE_MEMORY_ALIGNMENT;
    /* 残差信号のサイズ */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_samples_per_block);

    return work_size;
}

/* エンコーダハンドル作成 */
struct LINNEEncoder *LINNEEncoder_Create(const struct LINNEEncoderConfig *config, void *work, int32_t work_size)
{
    uint32_t ch, l;
    struct LINNEEncoder *encoder;
    uint8_t tmp_alloc_by_own = 0;
    uint8_t *work_ptr;

    /* ワーク領域時前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = LINNEEncoder_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((uint32_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < LINNEEncoder_CalculateWorkSize(config))) {
        return NULL;
    }

    /* コンフィグチェック */
    if ((config->max_num_channels == 0)
            || (config->max_num_samples_per_block == 0)
            || (config->max_num_layers == 0)
            || (config->max_num_parameters_per_layer == 0)) {
        return NULL;
    }

    /* ブロックサイズはパラメータ数より大きくなるべき */
    if (config->max_num_parameters_per_layer > config->max_num_samples_per_block) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t *)work;

    /* エンコーダハンドル領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    encoder = (struct LINNEEncoder *)work_ptr;
    work_ptr += sizeof(struct LINNEEncoder);

    /* エンコーダメンバ設定 */
    encoder->set_parameter = 0;
    encoder->alloced_by_own = tmp_alloc_by_own;
    encoder->work = work;
    encoder->max_num_channels = config->max_num_channels;
    encoder->max_num_samples_per_block = config->max_num_samples_per_block;
    encoder->max_num_layers = config->max_num_layers;
    encoder->max_num_parameters_per_layer = config->max_num_parameters_per_layer;

    /* 符号化ハンドルの作成 */
    {
        const int32_t coder_size = LINNECoder_CalculateWorkSize();
        if ((encoder->coder = LINNECoder_Create(work_ptr, coder_size)) == NULL) {
            return NULL;
        }
        work_ptr += coder_size;
    }

    /* ネットワークと領域確保 */
    {
        const int32_t network_size = LINNENetwork_CalculateWorkSize(
                config->max_num_samples_per_block, config->max_num_layers, config->max_num_parameters_per_layer);
        if ((encoder->network = LINNENetwork_Create(
                config->max_num_samples_per_block, config->max_num_layers,
                config->max_num_parameters_per_layer, work_ptr, network_size)) == NULL) {
            return NULL;
        }
        work_ptr += network_size;
    }

    /* トレーナーの領域確保 */
    {
        const int32_t trainer_size = LINNENetworkTrainer_CalculateWorkSize(
                config->max_num_layers, config->max_num_parameters_per_layer);
        if ((encoder->trainer = LINNENetworkTrainer_Create(
                config->max_num_layers, config->max_num_parameters_per_layer, work_ptr, trainer_size)) == NULL) {
            return NULL;
        }
        work_ptr += trainer_size;
    }

    /* プリエンファシスフィルタの作成 */
    LINNE_ALLOCATE_2DIMARRAY(encoder->pre_emphasis,
            work_ptr, struct LINNEPreemphasisFilter, config->max_num_channels, LINNE_NUM_PREEMPHASIS_FILTERS);
    /* プリエンファシスフィルタのバッファ領域 */
    LINNE_ALLOCATE_2DIMARRAY(encoder->pre_emphasis_prev,
            work_ptr, int32_t, config->max_num_channels, LINNE_NUM_PREEMPHASIS_FILTERS);

    /* バッファ領域の確保 全てのポインタをアラインメント */
    /* LPC係数(int) */
    LINNE_ALLOCATE_3DIMARRAY(encoder->params_int,
            work_ptr, int32_t, config->max_num_channels, config->max_num_layers, config->max_num_parameters_per_layer);
    /* LPC係数(double) */
    LINNE_ALLOCATE_3DIMARRAY(encoder->params_double,
            work_ptr, double, config->max_num_channels, config->max_num_layers, config->max_num_parameters_per_layer);
    /* 各層のユニット数 */
    LINNE_ALLOCATE_2DIMARRAY(encoder->num_units,
            work_ptr, uint32_t, config->max_num_channels, config->max_num_layers);
    /* 各層のLPC係数右シフト量 */
    LINNE_ALLOCATE_2DIMARRAY(encoder->rshifts,
            work_ptr, uint32_t, config->max_num_channels, config->max_num_layers);

    /* 信号処理用バッファ領域 */
    LINNE_ALLOCATE_2DIMARRAY(encoder->buffer_int,
            work_ptr, int32_t, config->max_num_channels, config->max_num_samples_per_block);
    LINNE_ALLOCATE_2DIMARRAY(encoder->residual,
            work_ptr, int32_t, config->max_num_channels, config->max_num_samples_per_block);

    /* doubleバッファ */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    encoder->buffer_double = (double *)work_ptr;
    work_ptr += config->max_num_samples_per_block * sizeof(double);

    /* バッファオーバーランチェック */
    /* 補足）既にメモリを破壊している可能性があるので、チェックに失敗したら落とす */
    LINNE_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    /* プリエンファシスフィルタ初期化 */
    for (ch = 0; ch < config->max_num_channels; ch++) {
        for (l = 0; l < LINNE_NUM_PREEMPHASIS_FILTERS; l++) {
            LINNEPreemphasisFilter_Initialize(&encoder->pre_emphasis[ch][l]);
        }
    }

    return encoder;
}

/* エンコーダハンドルの破棄 */
void LINNEEncoder_Destroy(struct LINNEEncoder *encoder)
{
    if (encoder != NULL) {
        LINNECoder_Destroy(encoder->coder);
        if (encoder->alloced_by_own == 1) {
            free(encoder->work);
        }
    }
}

/* エンコードパラメータの設定 */
LINNEApiResult LINNEEncoder_SetEncodeParameter(
        struct LINNEEncoder *encoder, const struct LINNEEncodeParameter *parameter)
{
    struct LINNEHeader tmp_header;

    /* 引数チェック */
    if ((encoder == NULL) || (parameter == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }

    /* パラメータ設定がおかしくないか、ヘッダへの変換を通じて確認 */
    /* 総サンプル数はダミー値を入れる */
    if (LINNEEncoder_ConvertParameterToHeader(parameter, 0, &tmp_header) != LINNE_ERROR_OK) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }

    /* エンコーダの容量を越えてないかチェック */
    if ((encoder->max_num_samples_per_block < parameter->num_samples_per_block)
            || (encoder->max_num_channels < parameter->num_channels)) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 最大レイヤー数/パラメータ数のチェック */
    {
        uint32_t i;
        const struct LINNEParameterPreset* preset = &g_linne_parameter_preset[parameter->preset];
        if (encoder->max_num_layers < preset->num_layers) {
            return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
        }
        for (i = 0; i < preset->num_layers; i++) {
            if (encoder->max_num_parameters_per_layer < preset->layer_num_params_list[i]) {
                return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
            }
        }
    }

    /* ヘッダ設定 */
    encoder->header = tmp_header;

    /* エンコードプリセットを取得 */
    LINNE_ASSERT(parameter->preset < LINNE_NUM_PARAMETER_PRESETS);
    encoder->parameter_preset = &g_linne_parameter_preset[parameter->preset];

    /* LPCネットのパラメータ設定 */
    LINNENetwork_SetLayerStructure(encoder->network,
            parameter->num_samples_per_block,
            encoder->parameter_preset->num_layers, encoder->parameter_preset->layer_num_params_list);

    /* 学習を行うかのフラグを立てる */
    encoder->enable_learning = parameter->enable_learning;

    /* 補助関数法の繰り返し回数をセット */
    encoder->num_afmethod_iterations = parameter->num_afmethod_iterations;


    /* 係数ハフマン符号構築 */
    {
        struct StaticHuffmanTree coef_tree;
        StaticHuffman_BuildHuffmanTree(
            encoder->parameter_preset->coef_symbol_freq_table, encoder->parameter_preset->num_coef_symbols, &coef_tree);
        StaticHuffman_ConvertTreeToCodes(&coef_tree, &encoder->coef_code);
    }

    /* パラメータ設定済みフラグを立てる */
    encoder->set_parameter = 1;

    return LINNE_APIRESULT_OK;
}

/* ブロックデータタイプの判定 */
static LINNEBlockDataType LINNEEncoder_DecideBlockDataType(
        struct LINNEEncoder *encoder, const int32_t *const *input, uint32_t num_samples)
{
    uint32_t ch, smpl;
    double mean_length;
    const struct LINNEHeader *header;

    LINNE_ASSERT(encoder != NULL);
    LINNE_ASSERT(input != NULL);
    LINNE_ASSERT(encoder->set_parameter == 1);

    header = &encoder->header;

    /* 平均符号長の計算 */
    mean_length = 0.0;
    for (ch = 0; ch < header->num_channels; ch++) {
        /* 入力をdouble化 */
        for (smpl = 0; smpl < num_samples; smpl++) {
            encoder->buffer_double[smpl] = input[ch][smpl] * pow(2.0, -(int32_t)(header->bits_per_sample - 1));
        }
        /* 推定符号長計算 */
        mean_length += LINNENetwork_EstimateCodeLength(encoder->network,
                encoder->buffer_double, num_samples, header->bits_per_sample);
    }
    mean_length /= header->num_channels;

    /* ビット幅に占める比に変換 */
    mean_length /= header->bits_per_sample;

    /* データタイプ判定 */

    /* 圧縮が効きにくい: 生データ出力 */
    if (mean_length >= LINNE_ESTIMATED_CODELENGTH_THRESHOLD) {
        return LINNE_BLOCK_DATA_TYPE_RAWDATA;
    }

    /* 無音判定 */
    for (ch = 0; ch < header->num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            if (input[ch][smpl] != 0) {
                goto NOT_SILENCE;
            }
        }
    }
    return LINNE_BLOCK_DATA_TYPE_SILENT;

NOT_SILENCE:
    /* それ以外は圧縮データ */
    return LINNE_BLOCK_DATA_TYPE_COMPRESSDATA;
}

/* 生データブロックエンコード */
static LINNEApiResult LINNEEncoder_EncodeRawData(
        struct LINNEEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint32_t ch, smpl;
    const struct LINNEHeader *header;
    uint8_t *data_ptr;

    /* 内部関数なので不正な引数はアサートで落とす */
    LINNE_ASSERT(encoder != NULL);
    LINNE_ASSERT(input != NULL);
    LINNE_ASSERT(num_samples > 0);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(data_size > 0);
    LINNE_ASSERT(output_size != NULL);

    header = &(encoder->header);

    /* 書き込み先のバッファサイズチェック */
    if (data_size < (header->bits_per_sample * num_samples * header->num_channels) / 8) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 生データをチャンネルインターリーブして出力 */
    data_ptr = data;
    switch (header->bits_per_sample) {
            case 8:
                for (smpl = 0; smpl < num_samples; smpl++) {
                    for (ch = 0; ch < header->num_channels; ch++) {
                        ByteArray_PutUint8(data_ptr, LINNEUTILITY_SINT32_TO_UINT32(input[ch][smpl]));
                        LINNE_ASSERT((uint32_t)(data_ptr - data) < data_size);
                    }
                }
                break;
            case 16:
                for (smpl = 0; smpl < num_samples; smpl++) {
                    for (ch = 0; ch < header->num_channels; ch++) {
                        ByteArray_PutUint16BE(data_ptr, LINNEUTILITY_SINT32_TO_UINT32(input[ch][smpl]));
                        LINNE_ASSERT((uint32_t)(data_ptr - data) < data_size);
                    }
                }
                break;
            case 24:
                for (smpl = 0; smpl < num_samples; smpl++) {
                    for (ch = 0; ch < header->num_channels; ch++) {
                        ByteArray_PutUint24BE(data_ptr, LINNEUTILITY_SINT32_TO_UINT32(input[ch][smpl]));
                        LINNE_ASSERT((uint32_t)(data_ptr - data) < data_size);
                    }
                }
                break;
            default:
                LINNE_ASSERT(0);
    }

    /* 書き込みサイズ取得 */
    (*output_size) = (uint32_t)(data_ptr - data);

    return LINNE_APIRESULT_OK;
}

/* 圧縮データブロックエンコード */
static LINNEApiResult LINNEEncoder_EncodeCompressData(
        struct LINNEEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint32_t ch, l, num_analyze_samples;
    struct BitStream writer;
    const struct LINNEHeader *header;

    /* 内部関数なので不正な引数はアサートで落とす */
    LINNE_ASSERT(encoder != NULL);
    LINNE_ASSERT(input != NULL);
    LINNE_ASSERT(num_samples > 0);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(data_size > 0);
    LINNE_ASSERT(output_size != NULL);

    header = &(encoder->header);

    /* 入力をバッファにコピー */
    for (ch = 0; ch < header->num_channels; ch++) {
        memcpy(encoder->buffer_int[ch], input[ch], sizeof(int32_t) * num_samples);
        /* バッファサイズより小さい入力のときは、末尾を0埋め */
        if (num_samples < encoder->max_num_samples_per_block) {
            const uint32_t remain = encoder->max_num_samples_per_block - num_samples;
            memset(&encoder->buffer_int[ch][num_samples], 0, sizeof(int32_t) * remain);
        }
    }

    /* マルチチャンネル処理 */
    if (header->ch_process_method == LINNE_CH_PROCESS_METHOD_MS) {
        /* チャンネル数チェック */
        if (header->num_channels < 2) {
            return LINNE_APIRESULT_INVALID_FORMAT;
        }
        /* LR -> MS */
        LINNEUtility_MSConversion(encoder->buffer_int, num_samples);
    }

    /* プリエンファシス */
    for (ch = 0; ch < header->num_channels; ch++) {
        for (l = 0; l < LINNE_NUM_PREEMPHASIS_FILTERS; l++) {
            /* 直前値には先頭の同一値が続くと考える */
            encoder->pre_emphasis[ch][l].prev = encoder->pre_emphasis_prev[ch][l] = encoder->buffer_int[ch][0];
            LINNEPreemphasisFilter_CalculateCoefficient(&encoder->pre_emphasis[ch][l], encoder->buffer_int[ch], num_samples);
            LINNEPreemphasisFilter_Preemphasis(&encoder->pre_emphasis[ch][l], encoder->buffer_int[ch], num_samples);
        }
    }

    /* LPCで分析するサンプル数を決定 */
    {
        uint32_t max_num_parameters_per_layer = 0;
        for (l = 0; l < encoder->parameter_preset->num_layers; l++) {
            if (max_num_parameters_per_layer < encoder->parameter_preset->layer_num_params_list[l]) {
                max_num_parameters_per_layer = encoder->parameter_preset->layer_num_params_list[l];
            }
        }
        /* ユニット数で割り切れるように、分析サンプル数はユニット分割数の倍数に切り上げ */
        num_analyze_samples = LINNEUTILITY_ROUNDUP(num_samples, (1 << LINNE_LOG2_NUM_UNITS_BITWIDTH));
        /* クリップ */
        num_analyze_samples = LINNEUTILITY_INNER_VALUE(num_analyze_samples, max_num_parameters_per_layer, header->num_samples_per_block);
    }

    /* チャンネル毎にLINNENetworkのパラメータ計算 */
    for (ch = 0; ch < header->num_channels; ch++) {
        uint32_t smpl;
        /* double精度の信号に変換（[-1,1]の範囲に正規化） */
        for (smpl = 0; smpl < num_analyze_samples; smpl++) {
            encoder->buffer_double[smpl] = encoder->buffer_int[ch][smpl] * pow(2.0, -(int32_t)(header->bits_per_sample - 1));
        }
        /* ユニット数とパラメータ設定 */
        LINNENetwork_SetUnitsAndParameters(encoder->network,
            encoder->buffer_double, num_analyze_samples,
            encoder->num_afmethod_iterations, encoder->parameter_preset->regular_terms_list, encoder->parameter_preset->num_regular_terms);
        /* ネットワーク学習 */
        if (encoder->enable_learning != 0) {
            LINNENetworkTrainer_Train(encoder->trainer,
                    encoder->network, encoder->buffer_double, num_analyze_samples,
                    LINNE_TRAINING_PARAMETER_MAX_NUM_ITRATION,
                    LINNE_TRAINING_PARAMETER_LEARNING_RATE,
                    LINNE_TRAINING_PARAMETER_LOSS_EPSILON);
        }
        /* ユニット数とパラメータ取得・量子化 */
        LINNENetwork_GetLayerNumUnits(encoder->network, encoder->num_units[ch], encoder->max_num_layers);
        LINNENetwork_GetParameters(encoder->network, encoder->params_double[ch], encoder->max_num_layers, encoder->max_num_parameters_per_layer);
        for (l = 0; l < encoder->parameter_preset->num_layers; l++) {
            LPC_QuantizeCoefficients(encoder->params_double[ch][l],
                    encoder->parameter_preset->layer_num_params_list[l], LINNE_LPC_COEFFICIENT_BITWIDTH,
                    encoder->params_int[ch][l], &encoder->rshifts[ch][l]);
        }
    }

    /* チャンネル毎にLPC予測 */
    for (ch = 0; ch < header->num_channels; ch++) {
        /* LPC予測 */
        for (l = 0; l < encoder->parameter_preset->num_layers; l++) {
            LINNELPC_Predict(encoder->buffer_int[ch],
                num_samples, encoder->params_int[ch][l], encoder->parameter_preset->layer_num_params_list[l],
                encoder->residual[ch], encoder->rshifts[ch][l], encoder->num_units[ch][l]);
            /* 残差を次のレイヤーの入力へ */
            memcpy(encoder->buffer_int[ch], encoder->residual[ch], sizeof(int32_t) * num_samples);
        }
    }

    /* ビットライタ作成 */
    BitWriter_Open(&writer, data, data_size);

    /* パラメータ符号化 */
    /* プリエンファシス */
    for (ch = 0; ch < header->num_channels; ch++) {
        uint32_t uval;
        for (l = 0; l < LINNE_NUM_PREEMPHASIS_FILTERS; l++) {
            /* プリエンファシスフィルタのバッファ */
            uval = LINNEUTILITY_SINT32_TO_UINT32(encoder->pre_emphasis_prev[ch][l]);
            LINNE_ASSERT(uval < (1 << (header->bits_per_sample + 1)));
            BitWriter_PutBits(&writer, uval, header->bits_per_sample + 1);
            /* プリエンファシス係数は正値に制限しているため1bitケチれる */
            LINNE_ASSERT(encoder->pre_emphasis[ch][l].coef >= 0);
            uval = (uint32_t)encoder->pre_emphasis[ch][l].coef;
            LINNE_ASSERT(uval < (1 << (LINNE_PREEMPHASIS_COEF_SHIFT - 1)));
            BitWriter_PutBits(&writer, uval, LINNE_PREEMPHASIS_COEF_SHIFT - 1);
        }
    }
    /* ユニット数/LPC係数右シフト量/LPC係数 */
    for (ch = 0; ch < header->num_channels; ch++) {
        for (l = 0; l < encoder->parameter_preset->num_layers; l++) {
            uint32_t i, uval;
            /* log2(ユニット数) */
            uval = LINNEUTILITY_LOG2CEIL(encoder->num_units[ch][l]);
            LINNE_ASSERT(uval < (1 << LINNE_LOG2_NUM_UNITS_BITWIDTH));
            BitWriter_PutBits(&writer, uval, LINNE_LOG2_NUM_UNITS_BITWIDTH);
            /* 各レイヤーでのLPC係数右シフト量: 基準のLPC_COEF_BITWIDTHと差分をとる */
            uval = LINNEUTILITY_SINT32_TO_UINT32(LINNE_LPC_COEFFICIENT_BITWIDTH - (int32_t)encoder->rshifts[ch][l]);
            LINNE_ASSERT(uval < (1 << LINNE_RSHIFT_LPC_COEFFICIENT_BITWIDTH));
            BitWriter_PutBits(&writer, uval, LINNE_RSHIFT_LPC_COEFFICIENT_BITWIDTH);
            /* LPC係数 */
            for (i = 0; i < encoder->parameter_preset->layer_num_params_list[l]; i++) {
                uval = LINNEUTILITY_SINT32_TO_UINT32(encoder->params_int[ch][l][i]);
                LINNE_ASSERT(uval < (1 << LINNE_LPC_COEFFICIENT_BITWIDTH));
                StaticHuffman_PutCode(&encoder->coef_code, &writer, uval);
            }
        }
    }

    /* 残差符号化 */
    for (ch = 0; ch < header->num_channels; ch++) {
        LINNECoder_Encode(encoder->coder, &writer, encoder->residual[ch], num_samples);
    }

    /* バイト境界に揃える */
    BitStream_Flush(&writer);

    /* 書き込みサイズの取得 */
    BitStream_Tell(&writer, (int32_t *)output_size);

    /* ビットライタ破棄 */
    BitStream_Close(&writer);

    return LINNE_APIRESULT_OK;
}

/* 無音データブロックエンコード */
static LINNEApiResult LINNEEncoder_EncodeSilentData(
        struct LINNEEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    /* 内部関数なので不正な引数はアサートで落とす */
    LINNE_ASSERT(encoder != NULL);
    LINNE_ASSERT(input != NULL);
    LINNE_ASSERT(num_samples > 0);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(data_size > 0);
    LINNE_ASSERT(output_size != NULL);

    /* データサイズなし */
    (*output_size) = 0;
    return LINNE_APIRESULT_OK;
}

/* 単一データブロックエンコード */
LINNEApiResult LINNEEncoder_EncodeBlock(
        struct LINNEEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint8_t *data_ptr;
    const struct LINNEHeader *header;
    LINNEBlockDataType block_type;
    LINNEApiResult ret;
    uint32_t block_header_size, block_data_size;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL) || (num_samples == 0)
            || (data == NULL) || (data_size == 0) || (output_size == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }
    header = &(encoder->header);

    /* パラメータがセットされてない */
    if (encoder->set_parameter != 1) {
        return LINNE_APIRESULT_PARAMETER_NOT_SET;
    }

    /* エンコードサンプル数チェック */
    if (num_samples > header->num_samples_per_block) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 圧縮手法の判定 */
    block_type = LINNEEncoder_DecideBlockDataType(encoder, input, num_samples);
    LINNE_ASSERT(block_type != LINNE_BLOCK_DATA_TYPE_INVALID);

    /* ブロックヘッダをエンコード */
    data_ptr = data;
    /* ブロック先頭の同期コード */
    ByteArray_PutUint16BE(data_ptr, LINNE_BLOCK_SYNC_CODE);
    /* ブロックサイズ: 仮値で埋めておく */
    ByteArray_PutUint32BE(data_ptr, 0);
    /* ブロックCRC16: 仮値で埋めておく */
    ByteArray_PutUint16BE(data_ptr, 0);
    /* ブロックデータタイプ */
    ByteArray_PutUint8(data_ptr, block_type);
    /* ブロックチャンネルあたりサンプル数 */
    ByteArray_PutUint16BE(data_ptr, num_samples);
    /* ブロックヘッダサイズ */
    block_header_size = (uint32_t)(data_ptr - data);

    /* データ部のエンコード */
    /* 手法によりエンコードする関数を呼び分け */
    switch (block_type) {
    case LINNE_BLOCK_DATA_TYPE_RAWDATA:
        ret = LINNEEncoder_EncodeRawData(encoder, input, num_samples,
                data_ptr, data_size - block_header_size, &block_data_size);
        break;
    case LINNE_BLOCK_DATA_TYPE_COMPRESSDATA:
        ret = LINNEEncoder_EncodeCompressData(encoder, input, num_samples,
                data_ptr, data_size - block_header_size, &block_data_size);
        break;
    case LINNE_BLOCK_DATA_TYPE_SILENT:
        ret = LINNEEncoder_EncodeSilentData(encoder, input, num_samples,
                data_ptr, data_size - block_header_size, &block_data_size);
        break;
    default:
        ret = LINNE_APIRESULT_INVALID_FORMAT;
        break;
    }

    /* エンコードに失敗している */
    if (ret != LINNE_APIRESULT_OK) {
        return ret;
    }

    /* ブロックサイズ書き込み:
    * CRC16(2byte) + ブロックチャンネルあたりサンプル数(2byte) + ブロックデータタイプ(1byte) */
    ByteArray_WriteUint32BE(&data[2], block_data_size + 5);

    /* CRC16の領域以降のCRC16を計算し書き込み */
    {
        /* ブロックチャンネルあたりサンプル数(2byte) + ブロックデータタイプ(1byte) を加算 */
        const uint16_t crc16 = LINNEUtility_CalculateCRC16(&data[8], block_data_size + 3);
        ByteArray_WriteUint16BE(&data[6], crc16);
    }

    /* 出力サイズ */
    (*output_size) = block_header_size + block_data_size;

    /* エンコード成功 */
    return LINNE_APIRESULT_OK;
}

/* ヘッダ含めファイル全体をエンコード */
LINNEApiResult LINNEEncoder_EncodeWhole(
        struct LINNEEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    LINNEApiResult ret;
    uint32_t progress, ch, write_size, write_offset, num_encode_samples;
    uint8_t *data_pos;
    const int32_t *input_ptr[LINNE_MAX_NUM_CHANNELS];
    const struct LINNEHeader *header;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL)
            || (data == NULL) || (output_size == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }

    /* パラメータがセットされてない */
    if (encoder->set_parameter != 1) {
        return LINNE_APIRESULT_PARAMETER_NOT_SET;
    }

    /* 書き出し位置を取得 */
    data_pos = data;

    /* ヘッダエンコード */
    encoder->header.num_samples = num_samples;
    if ((ret = LINNEEncoder_EncodeHeader(&(encoder->header), data_pos, data_size))
            != LINNE_APIRESULT_OK) {
        return ret;
    }
    header = &(encoder->header);

    /* 進捗状況初期化 */
    progress = 0;
    write_offset = LINNE_HEADER_SIZE;
    data_pos = data + LINNE_HEADER_SIZE;

    /* ブロックを時系列順にエンコード */
    while (progress < num_samples) {

        /* エンコードサンプル数の確定 */
        num_encode_samples
            = LINNEUTILITY_MIN(header->num_samples_per_block, num_samples - progress);

        /* サンプル参照位置のセット */
        for (ch = 0; ch < header->num_channels; ch++) {
            input_ptr[ch] = &input[ch][progress];
        }

        /* ブロックエンコード */
        if ((ret = LINNEEncoder_EncodeBlock(encoder,
                        input_ptr, num_encode_samples,
                        data_pos, data_size - write_offset, &write_size)) != LINNE_APIRESULT_OK) {
            return ret;
        }

        /* 進捗更新 */
        data_pos      += write_size;
        write_offset  += write_size;
        progress      += num_encode_samples;
        LINNE_ASSERT(write_offset <= data_size);
    }

    /* 成功終了 */
    (*output_size) = write_offset;
    return LINNE_APIRESULT_OK;
}
