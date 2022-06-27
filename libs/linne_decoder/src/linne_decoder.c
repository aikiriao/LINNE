#include "linne_decoder.h"

#include <stdlib.h>
#include <string.h>
#include "linne_lpc_synthesize.h"
#include "linne_internal.h"
#include "linne_utility.h"
#include "linne_coder.h"
#include "byte_array.h"
#include "bit_stream.h"
#include "lpc.h"
#include "static_huffman.h"

/* 内部状態フラグ */
#define LINNEDECODER_STATUS_FLAG_ALLOCED_BY_OWN  (1 << 0)  /* 領域を自己割当した */
#define LINNEDECODER_STATUS_FLAG_SET_HEADER      (1 << 1)  /* ヘッダセット済み */
#define LINNEDECODER_STATUS_FLAG_CRC16_CHECK     (1 << 2)  /* CRC16の検査を行う */

/* 内部状態フラグ操作マクロ */
#define LINNEDECODER_SET_STATUS_FLAG(decoder, flag)    ((decoder->status_flags) |= (flag))
#define LINNEDECODER_CLEAR_STATUS_FLAG(decoder, flag)  ((decoder->status_flags) &= ~(flag))
#define LINNEDECODER_GET_STATUS_FLAG(decoder, flag)    ((decoder->status_flags) & (flag))

/* デコーダハンドル */
struct LINNEDecoder {
    struct LINNEHeader header; /* ヘッダ */
    uint32_t max_num_channels; /* デコード可能な最大チャンネル数 */
    uint32_t max_num_layers; /* 最大レイヤー数 */
    uint32_t max_num_parameters_per_layer; /* 最大レイヤーあたりパラメータ数 */
    struct LINNEPreemphasisFilter **de_emphasis; /* デエンファシスフィルタ */
    int32_t ***params_int; /* LPC係数(int) */
    uint32_t **num_units; /* 各層のユニット数 */
    uint32_t **rshifts; /* 各層のLPC係数右シフト量 */
    const struct LINNEParameterPreset *parameter_preset; /* パラメータプリセット */
    struct StaticHuffmanTree coef_tree; /* 係数ハフマン木 */
    uint8_t status_flags; /* 内部状態フラグ */
    void *work; /* ワーク領域先頭ポインタ */
};

/* 生データブロックデコード */
static LINNEApiResult LINNEDecoder_DecodeRawData(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size);
/* 圧縮データブロックデコード */
static LINNEApiResult LINNEDecoder_DecodeCompressData(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size);
/* 無音データブロックデコード */
static LINNEApiResult LINNEDecoder_DecodeSilentData(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size);

/* ヘッダデコード */
LINNEApiResult LINNEDecoder_DecodeHeader(
        const uint8_t *data, uint32_t data_size, struct LINNEHeader *header)
{
    const uint8_t *data_pos;
    uint32_t u32buf;
    uint16_t u16buf;
    uint8_t  u8buf;
    struct LINNEHeader tmp_header;

    /* 引数チェック */
    if ((data == NULL) || (header == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }

    /* データサイズが足りない */
    if (data_size < LINNE_HEADER_SIZE) {
        return LINNE_APIRESULT_INSUFFICIENT_DATA;
    }

    /* 読み出し用ポインタ設定 */
    data_pos = data;

    /* シグネチャ */
    {
        uint8_t buf[4];
        ByteArray_GetUint8(data_pos, &buf[0]);
        ByteArray_GetUint8(data_pos, &buf[1]);
        ByteArray_GetUint8(data_pos, &buf[2]);
        ByteArray_GetUint8(data_pos, &buf[3]);
        if ((buf[0] != 'I') || (buf[1] != 'B')
                || (buf[2] != 'R') || (buf[3] != 'A')) {
            return LINNE_APIRESULT_INVALID_FORMAT;
        }
    }

    /* シグネチャ検査に通ったら、エラーを起こさずに読み切る */

    /* フォーマットバージョン */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.format_version = u32buf;
    /* エンコーダバージョン */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.codec_version = u32buf;
    /* チャンネル数 */
    ByteArray_GetUint16BE(data_pos, &u16buf);
    tmp_header.num_channels = u16buf;
    /* サンプル数 */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.num_samples = u32buf;
    /* サンプリングレート */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.sampling_rate = u32buf;
    /* サンプルあたりビット数 */
    ByteArray_GetUint16BE(data_pos, &u16buf);
    tmp_header.bits_per_sample = u16buf;
    /* 最大ブロックあたりサンプル数 */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.num_samples_per_block = u32buf;
    /* パラメータプリセット */
    ByteArray_GetUint8(data_pos, &u8buf);
    tmp_header.preset = u8buf;
    /* マルチチャンネル処理法 */
    ByteArray_GetUint8(data_pos, &u8buf);
    tmp_header.ch_process_method = (LINNEChannelProcessMethod)u8buf;

    /* ヘッダサイズチェック */
    LINNE_ASSERT((data_pos - data) == LINNE_HEADER_SIZE);

    /* 成功終了 */
    (*header) = tmp_header;
    return LINNE_APIRESULT_OK;
}

/* ヘッダのフォーマットチェック */
static LINNEError LINNEDecoder_CheckHeaderFormat(const struct LINNEHeader *header)
{
    /* 内部モジュールなのでNULLが渡されたら落とす */
    LINNE_ASSERT(header != NULL);

    /* フォーマットバージョン */
    /* 補足）今のところは不一致なら無条件でエラー */
    if (header->format_version != LINNE_FORMAT_VERSION) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* コーデックバージョン */
    /* 補足）今のところは不一致なら無条件でエラー */
    if (header->codec_version != LINNE_CODEC_VERSION) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* チャンネル数 */
    if (header->num_channels == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* サンプル数 */
    if (header->num_samples == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* サンプリングレート */
    if (header->sampling_rate == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* ビット深度 */
    if (header->bits_per_sample == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* ブロックあたりサンプル数 */
    if (header->num_samples_per_block == 0) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* パラメータプリセット */
    if (header->preset >= LINNE_NUM_PARAMETER_PRESETS) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* マルチチャンネル処理法 */
    if (header->ch_process_method >= LINNE_CH_PROCESS_METHOD_INVALID) {
        return LINNE_ERROR_INVALID_FORMAT;
    }
    /* モノラルではMS処理はできない */
    if ((header->ch_process_method == LINNE_CH_PROCESS_METHOD_MS)
            && (header->num_channels == 1)) {
        return LINNE_ERROR_INVALID_FORMAT;
    }

    return LINNE_ERROR_OK;
}

/* デコーダハンドルの作成に必要なワークサイズの計算 */
int32_t LINNEDecoder_CalculateWorkSize(const struct LINNEDecoderConfig *config)
{
    int32_t work_size, tmp_work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if ((config->max_num_channels == 0)
            || (config->max_num_layers == 0)
            || (config->max_num_parameters_per_layer == 0)) {
        return -1;
    }

    /* 構造体サイズ（+メモリアラインメント） */
    work_size = sizeof(struct LINNEDecoder) + LINNE_MEMORY_ALIGNMENT;
    /* デエンファシスフィルタのサイズ */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(struct LINNEPreemphasisFilter, config->max_num_channels, LINNE_NUM_PREEMPHASIS_FILTERS);
    /* パラメータ領域 */
    /* LPC係数(int) */
    work_size += LINNE_CALCULATE_3DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_layers, config->max_num_parameters_per_layer);
    /* 各層のユニット数 */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(uint32_t, config->max_num_channels, config->max_num_layers);
    /* 各層のLPC係数右シフト量 */
    work_size += LINNE_CALCULATE_2DIMARRAY_WORKSIZE(uint32_t, config->max_num_channels, config->max_num_layers);

    return work_size;
}

/* デコーダハンドル作成 */
struct LINNEDecoder *LINNEDecoder_Create(const struct LINNEDecoderConfig *config, void *work, int32_t work_size)
{
    uint32_t ch, l;
    struct LINNEDecoder *decoder;
    uint8_t *work_ptr;
    uint8_t tmp_alloc_by_own = 0;

    /* 領域自前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = LINNEDecoder_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((uint32_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < LINNEDecoder_CalculateWorkSize(config))) {
        return NULL;
    }

    /* コンフィグチェック */
    if ((config->max_num_channels == 0)
            || (config->max_num_layers == 0)
            || (config->max_num_parameters_per_layer == 0)) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    decoder = (struct LINNEDecoder *)work_ptr;
    work_ptr += sizeof(struct LINNEDecoder);

    /* 構造体メンバセット */
    decoder->work = work;
    decoder->max_num_channels = config->max_num_channels;
    decoder->max_num_layers = config->max_num_layers;
    decoder->max_num_parameters_per_layer = config->max_num_parameters_per_layer;
    decoder->status_flags = 0;  /* 状態クリア */
    if (tmp_alloc_by_own == 1) {
        LINNEDECODER_SET_STATUS_FLAG(decoder, LINNEDECODER_STATUS_FLAG_ALLOCED_BY_OWN);
    }
    if (config->check_crc == 1) {
        LINNEDECODER_SET_STATUS_FLAG(decoder, LINNEDECODER_STATUS_FLAG_CRC16_CHECK);
    }

    /* デエンファシスフィルタの作成 */
    LINNE_ALLOCATE_2DIMARRAY(decoder->de_emphasis,
            work_ptr, struct LINNEPreemphasisFilter, config->max_num_channels, LINNE_NUM_PREEMPHASIS_FILTERS);

    /* バッファ領域の確保 全てのポインタをアラインメント */
    /* LPC係数(int) */
    LINNE_ALLOCATE_3DIMARRAY(decoder->params_int,
            work_ptr, int32_t, config->max_num_channels, config->max_num_layers, config->max_num_parameters_per_layer);
    /* 各層のユニット数 */
    LINNE_ALLOCATE_2DIMARRAY(decoder->num_units,
            work_ptr, uint32_t, config->max_num_channels, config->max_num_layers);
    /* 各層のLPC係数右シフト量 */
    LINNE_ALLOCATE_2DIMARRAY(decoder->rshifts,
            work_ptr, uint32_t, config->max_num_channels, config->max_num_layers);

    /* バッファオーバーランチェック */
    /* 補足）既にメモリを破壊している可能性があるので、チェックに失敗したら落とす */
    LINNE_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    /* プリエンファシスフィルタ初期化 */
    for (ch = 0; ch < config->max_num_channels; ch++) {
        for (l = 0; l < LINNE_NUM_PREEMPHASIS_FILTERS; l++) {
            LINNEPreemphasisFilter_Initialize(&decoder->de_emphasis[ch][l]);
        }
    }

    return decoder;
}

/* デコーダハンドルの破棄 */
void LINNEDecoder_Destroy(struct LINNEDecoder *decoder)
{
    if (decoder != NULL) {
        if (LINNEDECODER_GET_STATUS_FLAG(decoder, LINNEDECODER_STATUS_FLAG_ALLOCED_BY_OWN)) {
            free(decoder->work);
        }
    }
}

/* デコーダにヘッダをセット */
LINNEApiResult LINNEDecoder_SetHeader(
        struct LINNEDecoder *decoder, const struct LINNEHeader *header)
{
    /* 引数チェック */
    if ((decoder == NULL) || (header == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダの有効性チェック */
    if (LINNEDecoder_CheckHeaderFormat(header) != LINNE_ERROR_OK) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }

    /* デコーダの容量を越えてないかチェック */
    if (decoder->max_num_channels < header->num_channels) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 最大レイヤー数/パラメータ数のチェック */
    {
        uint32_t i;
        const struct LINNEParameterPreset* preset = &g_linne_parameter_preset[header->preset];
        if (decoder->max_num_layers < preset->num_layers) {
            return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
        }
        for (i = 0; i < preset->num_layers; i++) {
            if (decoder->max_num_parameters_per_layer < preset->layer_num_params_list[i]) {
                return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
            }
        }
    }

    /* エンコードプリセットを取得 */
    LINNE_ASSERT(header->preset < LINNE_NUM_PARAMETER_PRESETS);
    decoder->parameter_preset = &g_linne_parameter_preset[header->preset];

    /* 係数ハフマン木構築 */
    StaticHuffman_BuildHuffmanTree(
        decoder->parameter_preset->coef_symbol_freq_table, decoder->parameter_preset->num_coef_symbols, &decoder->coef_tree);

    /* ヘッダセット */
    decoder->header = (*header);
    LINNEDECODER_SET_STATUS_FLAG(decoder, LINNEDECODER_STATUS_FLAG_SET_HEADER);

    return LINNE_APIRESULT_OK;
}

/* 生データブロックデコード */
static LINNEApiResult LINNEDecoder_DecodeRawData(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size)
{
    uint32_t ch, smpl;
    const struct LINNEHeader *header;
    const uint8_t *read_ptr;

    /* 内部関数なので不正な引数はアサートで落とす */
    LINNE_ASSERT(decoder != NULL);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(data_size > 0);
    LINNE_ASSERT(buffer != NULL);
    LINNE_ASSERT(buffer[0] != NULL);
    LINNE_ASSERT(num_decode_samples > 0);
    LINNE_ASSERT(decode_size != NULL);

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* チャンネル数不足もアサートで落とす */
    LINNE_ASSERT(num_channels >= header->num_channels);

    /* データサイズチェック */
    if (data_size < (header->bits_per_sample * num_decode_samples * header->num_channels) / 8) {
        return LINNE_APIRESULT_INSUFFICIENT_DATA;
    }

    /* 生データをチャンネルインターリーブで取得 */
    read_ptr = data;
    switch (header->bits_per_sample) {
    case 8:
        for (smpl = 0; smpl < num_decode_samples; smpl++) {
            for (ch = 0; ch < header->num_channels; ch++) {
                uint8_t buf;
                ByteArray_GetUint8(read_ptr, &buf);
                buffer[ch][smpl] = LINNEUTILITY_UINT32_TO_SINT32(buf);
                LINNE_ASSERT((uint32_t)(read_ptr - data) <= data_size);
            }
        }
        break;
    case 16:
        for (smpl = 0; smpl < num_decode_samples; smpl++) {
            for (ch = 0; ch < header->num_channels; ch++) {
                uint16_t buf;
                ByteArray_GetUint16BE(read_ptr, &buf);
                buffer[ch][smpl] = LINNEUTILITY_UINT32_TO_SINT32(buf);
                LINNE_ASSERT((uint32_t)(read_ptr - data) <= data_size);
            }
        }
        break;
    case 24:
        for (smpl = 0; smpl < num_decode_samples; smpl++) {
            for (ch = 0; ch < header->num_channels; ch++) {
                uint32_t buf;
                ByteArray_GetUint24BE(read_ptr, &buf);
                buffer[ch][smpl] = LINNEUTILITY_UINT32_TO_SINT32(buf);
                LINNE_ASSERT((uint32_t)(read_ptr - data) <= data_size);
            }
        }
        break;
    default: LINNE_ASSERT(0);
    }

    /* 読み取りサイズ取得 */
    (*decode_size) = (uint32_t)(read_ptr - data);

    return LINNE_APIRESULT_OK;
}

/* 圧縮データブロックデコード */
static LINNEApiResult LINNEDecoder_DecodeCompressData(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size)
{
    uint32_t ch;
    int32_t l;
    struct BitStream reader;
    const struct LINNEHeader *header;

    /* 内部関数なので不正な引数はアサートで落とす */
    LINNE_ASSERT(decoder != NULL);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(data_size > 0);
    LINNE_ASSERT(buffer != NULL);
    LINNE_ASSERT(buffer[0] != NULL);
    LINNE_ASSERT(num_decode_samples > 0);
    LINNE_ASSERT(decode_size != NULL);

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* チャンネル数不足もアサートで落とす */
    LINNE_ASSERT(num_channels >= header->num_channels);

    /* ビットリーダ作成 */
    BitReader_Open(&reader, (uint8_t *)data, data_size);

    /* パラメータ復号 */
    /* プリエンファシス */
    for (ch = 0; ch < num_channels; ch++) {
        uint32_t uval;
        for (l = 0; l < LINNE_NUM_PREEMPHASIS_FILTERS; l++) {
            BitReader_GetBits(&reader, &uval, header->bits_per_sample + 1);
            decoder->de_emphasis[ch][l].prev = LINNEUTILITY_UINT32_TO_SINT32(uval);
            /* プリエンファシス係数は正値に制限しているため1bitケチれる */
            BitReader_GetBits(&reader, &uval, LINNE_PREEMPHASIS_COEF_SHIFT - 1);
            decoder->de_emphasis[ch][l].coef = (int32_t)uval;
        }
    }
    /* ユニット数/LPC係数右シフト量/LPC係数 */
    for (ch = 0; ch < num_channels; ch++) {
        for (l = 0; l < (int32_t)decoder->parameter_preset->num_layers; l++) {
            uint32_t i, uval;
            /* log2(ユニット数) */
            BitReader_GetBits(&reader, &uval, LINNE_LOG2_NUM_UNITS_BITWIDTH);
            decoder->num_units[ch][l] = (1 << uval);
            /* 各レイヤーでのLPC係数右シフト量: 基準のLINNE_LPC_COEFFICIENT_BITWIDTHと差分をとる */
            BitReader_GetBits(&reader, &uval, LINNE_RSHIFT_LPC_COEFFICIENT_BITWIDTH);
            decoder->rshifts[ch][l] = (uint32_t)(LINNE_LPC_COEFFICIENT_BITWIDTH - LINNEUTILITY_UINT32_TO_SINT32(uval));
            /* LPC係数 */
            for (i = 0; i < decoder->parameter_preset->layer_num_params_list[l]; i++) {
                uval = StaticHuffman_GetCode(&decoder->coef_tree, &reader);
                decoder->params_int[ch][l][i] = LINNEUTILITY_UINT32_TO_SINT32(uval);
            }
        }
    }

    /* 残差復号 */
    for (ch = 0; ch < header->num_channels; ch++) {
        LINNECoder_Decode(&reader, buffer[ch], num_decode_samples);
    }

    /* バイト境界に揃える */
    BitStream_Flush(&reader);

    /* 読み出しサイズの取得 */
    BitStream_Tell(&reader, (int32_t *)decode_size);

    /* ビットライタ破棄 */
    BitStream_Close(&reader);

    /* チャンネル毎に合成処理 */
    for (ch = 0; ch < header->num_channels; ch++) {
        /* LPC合成 */
        for (l = (int32_t)decoder->parameter_preset->num_layers - 1; l >= 0; l--) {
            uint32_t u;
            const uint32_t nunits = decoder->num_units[ch][l];
            const uint32_t nparams_per_unit = decoder->parameter_preset->layer_num_params_list[l] / nunits;
            const uint32_t nsmpls_per_unit = num_decode_samples / nunits;
            const uint32_t rshift = decoder->rshifts[ch][l];
            for (u = 0; u < nunits; u++) {
                int32_t *poutput = &buffer[ch][u * nsmpls_per_unit];
                const int32_t *pcoef = &decoder->params_int[ch][l][u * nparams_per_unit];
                /* 合成 */
                LINNELPC_Synthesize(poutput, nsmpls_per_unit, pcoef, nparams_per_unit, rshift, u == 0);
            }
        }
        /* デエンファシス */
        for (l = LINNE_NUM_PREEMPHASIS_FILTERS - 1; l >= 0; l--) {
            LINNEPreemphasisFilter_Deemphasis(&decoder->de_emphasis[ch][l], buffer[ch], num_decode_samples);
        }
    }

    /* MS -> LR */
    if (header->ch_process_method == LINNE_CH_PROCESS_METHOD_MS) {
        /* チャンネル数チェック */
        if (header->num_channels < 2) {
            return LINNE_APIRESULT_INVALID_FORMAT;
        }
        LINNEUtility_LRConversion(buffer, num_decode_samples);
    }

    /* 成功終了 */
    return LINNE_APIRESULT_OK;
}

/* 無音データブロックデコード */
static LINNEApiResult LINNEDecoder_DecodeSilentData(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size)
{
    uint32_t ch;
    const struct LINNEHeader *header;

    LINNEUTILITY_UNUSED_ARGUMENT(data_size);

    /* 内部関数なので不正な引数はアサートで落とす */
    LINNE_ASSERT(decoder != NULL);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(buffer != NULL);
    LINNE_ASSERT(buffer[0] != NULL);
    LINNE_ASSERT(num_decode_samples > 0);
    LINNE_ASSERT(decode_size != NULL);

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* チャンネル数不足もアサートで落とす */
    LINNE_ASSERT(num_channels >= header->num_channels);

    /* 全て無音で埋める */
    for (ch = 0; ch < header->num_channels; ch++) {
        memset(buffer[ch], 0, sizeof(int32_t) * num_decode_samples);
    }

    (*decode_size) = 0;
    return LINNE_APIRESULT_OK;
}

/* 単一データブロックデコード */
LINNEApiResult LINNEDecoder_DecodeBlock(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples,
        uint32_t *decode_size, uint32_t *num_decode_samples)
{
    uint8_t buf8;
    uint16_t buf16;
    uint32_t buf32;
    uint16_t num_block_samples;
    uint32_t block_header_size, block_data_size;
    LINNEApiResult ret;
    LINNEBlockDataType block_type;
    const struct LINNEHeader *header;
    const uint8_t *read_ptr;

    /* 引数チェック */
    if ((decoder == NULL) || (data == NULL)
            || (buffer == NULL) || (decode_size == NULL)
            || (num_decode_samples == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダがまだセットされていない */
    if (!LINNEDECODER_GET_STATUS_FLAG(decoder, LINNEDECODER_STATUS_FLAG_SET_HEADER)) {
        return LINNE_APIRESULT_PARAMETER_NOT_SET;
    }

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* バッファチャンネル数チェック */
    if (buffer_num_channels < header->num_channels) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* ブロックヘッダデコード */
    read_ptr = data;

    /* 同期コード */
    ByteArray_GetUint16BE(read_ptr, &buf16);
    /* 同期コード不一致 */
    if (buf16 != LINNE_BLOCK_SYNC_CODE) {
        return LINNE_APIRESULT_INVALID_FORMAT;
    }
    /* ブロックサイズ */
    ByteArray_GetUint32BE(read_ptr, &buf32);
    LINNE_ASSERT(buf32 > 0);
    /* データサイズ不足 */
    if ((buf32 + 6) > data_size) {
        return LINNE_APIRESULT_INSUFFICIENT_DATA;
    }
    /* ブロックCRC16 */
    ByteArray_GetUint16BE(read_ptr, &buf16);
    /* チェックするならばCRC16計算を行い取得値との一致を確認 */
    if (LINNEDECODER_GET_STATUS_FLAG(decoder, LINNEDECODER_STATUS_FLAG_CRC16_CHECK)) {
        /* CRC16自体の領域は外すために-2 */
        uint16_t crc16 = LINNEUtility_CalculateCRC16(read_ptr, buf32 - 2);
        if (crc16 != buf16) {
            return LINNE_APIRESULT_DETECT_DATA_CORRUPTION;
        }
    }
    /* ブロックデータタイプ */
    ByteArray_GetUint8(read_ptr, &buf8);
    block_type = (LINNEBlockDataType)buf8;
    /* ブロックチャンネルあたりサンプル数 */
    ByteArray_GetUint16BE(read_ptr, &num_block_samples);
    if (num_block_samples > buffer_num_samples) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }
    /* ブロックヘッダサイズ */
    block_header_size = (uint32_t)(read_ptr - data);

    /* データ部のデコード */
    switch (block_type) {
    case LINNE_BLOCK_DATA_TYPE_RAWDATA:
        ret = LINNEDecoder_DecodeRawData(decoder,
                read_ptr, data_size - block_header_size, buffer, header->num_channels, num_block_samples, &block_data_size);
        break;
    case LINNE_BLOCK_DATA_TYPE_COMPRESSDATA:
        ret = LINNEDecoder_DecodeCompressData(decoder,
                read_ptr, data_size - block_header_size, buffer, header->num_channels, num_block_samples, &block_data_size);
        break;
    case LINNE_BLOCK_DATA_TYPE_SILENT:
        ret = LINNEDecoder_DecodeSilentData(decoder,
                read_ptr, data_size - block_header_size, buffer, header->num_channels, num_block_samples, &block_data_size);
        break;
    default:
        return LINNE_APIRESULT_INVALID_FORMAT;
    }

    /* データデコードに失敗している */
    if (ret != LINNE_APIRESULT_OK) {
        return ret;
    }

    /* デコードサイズ */
    (*decode_size) = block_header_size + block_data_size;

    /* デコードサンプル数 */
    (*num_decode_samples) = num_block_samples;

    /* デコード成功 */
    return LINNE_APIRESULT_OK;
}

/* ヘッダを含めて全ブロックデコード */
LINNEApiResult LINNEDecoder_DecodeWhole(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples)
{
    LINNEApiResult ret;
    uint32_t progress, ch, read_offset, read_block_size, num_decode_samples;
    const uint8_t *read_pos;
    int32_t *buffer_ptr[LINNE_MAX_NUM_CHANNELS];
    struct LINNEHeader tmp_header;
    const struct LINNEHeader *header;

    /* 引数チェック */
    if ((decoder == NULL) || (data == NULL) || (buffer == NULL)) {
        return LINNE_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダデコードとデコーダへのセット */
    if ((ret = LINNEDecoder_DecodeHeader(data, data_size, &tmp_header))
            != LINNE_APIRESULT_OK) {
        return ret;
    }
    if ((ret = LINNEDecoder_SetHeader(decoder, &tmp_header))
            != LINNE_APIRESULT_OK) {
        return ret;
    }
    header = &(decoder->header);

    /* バッファサイズチェック */
    if ((buffer_num_channels < header->num_channels)
            || (buffer_num_samples < header->num_samples)) {
        return LINNE_APIRESULT_INSUFFICIENT_BUFFER;
    }

    progress = 0;
    read_offset = LINNE_HEADER_SIZE;
    read_pos = data + LINNE_HEADER_SIZE;
    while ((progress < header->num_samples) && (read_offset < data_size)) {
        /* サンプル書き出し位置のセット */
        for (ch = 0; ch < header->num_channels; ch++) {
            buffer_ptr[ch] = &buffer[ch][progress];
        }
        /* ブロックデコード */
        if ((ret = LINNEDecoder_DecodeBlock(decoder,
                        read_pos, data_size - read_offset,
                        buffer_ptr, buffer_num_channels, buffer_num_samples - progress,
                        &read_block_size, &num_decode_samples)) != LINNE_APIRESULT_OK) {
            return ret;
        }
        /* 進捗更新 */
        read_pos    += read_block_size;
        read_offset += read_block_size;
        progress    += num_decode_samples;
        LINNE_ASSERT(progress <= buffer_num_samples);
        LINNE_ASSERT(read_offset <= data_size);
    }

    /* 成功終了 */
    return LINNE_APIRESULT_OK;
}
