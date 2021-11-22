#include "lpcnet.h"
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <float.h>
#include "lpc.h"

/* メモリアラインメント */
#define LPCNET_MEMORY_ALIGNMENT 16

/* レイヤーあたり最大パラメータ数 */
#define LPCNET_MAX_PARAMS_PER_LAYER 128

/* 符号関数: x > 0ならば1, x < 0ならば-1, x==0ならば0 */
#define LPCNET_SIGN(x) (((x) > 0) - ((x) < 0))

/* 2の冪数(1, 2, 4, 8, ...)か判定 */
#define LPCNET_IS_POWERED_OF_2(x) (!((x) & ((x) - 1)))

/* nの倍数への切り上げ */
#define LPCNET_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))

/* アサートマクロ */
#ifdef NDEBUG
/* 未使用変数警告を明示的に回避 */
#define LPCNET_ASSERT(condition) ((void)(condition))
#else
#include <assert.h>
#define LPCNET_ASSERT(condition) assert(condition)
#endif

/* LPCネットを構成するレイヤー */
struct LPCNetLayer {
    double *din; /* 入力信号バッファ */
    double *dout; /* 逆伝播信号バッファ */
    double *params; /* パラメータ（LPC係数） */
    double *dparams; /* パラメータ勾配 */
    uint32_t num_samples; /* 入力サンプル数 */
    uint32_t num_params; /* レイヤー内の全パラメータ数 */
    uint32_t num_units; /* レイヤー内のユニット数 */
};

/* LPCネット */
struct LPCNet {
    struct LPCNetLayer **layers; /* レイヤー配列 */
    void *layers_work; /* レイヤー配列の先頭領域 */
    uint32_t max_num_samples; /* 最大サンプル数 */
    int32_t max_num_layers; /* 最大レイヤー（層）数 */
    uint32_t max_num_params; /* 最大レイヤーあたりパラメータ数 */
    struct LPCCalculator *lpcc; /* LPC係数計算ハンドル */
    double *data_buffer; /* 入力データバッファ */
    uint32_t num_samples; /* 入力サンプル数 */
    int32_t num_layers; /* レイヤー数 */
};

/* LPCネットトレーナー */
struct LPCNetTrainer {
    uint32_t max_num_layers; /* 最大層数 */
    uint32_t max_num_params_per_layer; /* レイヤーあたりパラメータ数 */
    double **momentum; /* モーメンタム */
    double momentum_alpha; /* モーメンタムのハイパラ */
#if 0
    double **grad_rs; /* 勾配の各要素の2乗和 */
    double **m; /* Adamの速度項 */
    double **v; /* Adamの勾配の2乗和項 */
    double beta1, beta2; /* Adamのハイパラ */
#endif
};

/* L1ノルムレイヤーのロス計算 */
static double LPCL1Norm_Loss(const double *data, uint32_t num_samples)
{
    uint32_t smpl;
    double norm = 0.0f;

    LPCNET_ASSERT(data != NULL);
    LPCNET_ASSERT(num_samples > 0);

    for (smpl = 0; smpl < num_samples; smpl++) {
        norm += fabs(data[smpl]);
    }

    return norm / num_samples;
}

/* L1ノルムレイヤーの誤差逆伝播 */
static void LPCL1Norm_Backward(double *data, uint32_t num_samples)
{
    uint32_t smpl;

    LPCNET_ASSERT(data != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        data[smpl] = (double)LPCNET_SIGN(data[smpl]) / num_samples;
    }
}

/* LPCネットレイヤー作成に必要なワークサイズ計算 */
static int32_t LPCNetLayer_CalculateWorkSize(uint32_t num_samples, uint32_t num_params)
{
    int32_t work_size;

    work_size = sizeof(struct LPCNetLayer) + LPCNET_MEMORY_ALIGNMENT;
    work_size += 2 * (sizeof(double) * num_samples + LPCNET_MEMORY_ALIGNMENT);
    work_size += 2 * (sizeof(double) * num_params + LPCNET_MEMORY_ALIGNMENT);

    return work_size;
}

/* LPCネットレイヤー作成 */
static struct LPCNetLayer *LPCNetLayer_Create(
        uint32_t num_samples, uint32_t num_params, void *work, int32_t work_size)
{
    uint32_t i;
    struct LPCNetLayer *layer;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((work == NULL)
            || (work_size < LPCNetLayer_CalculateWorkSize(num_samples, num_params))) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    layer = (struct LPCNetLayer *)work_ptr;
    work_ptr += sizeof(struct LPCNetLayer);
    layer->num_samples = num_samples;
    layer->num_params = num_params;

    /* 入出力バッファ領域確保 */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    layer->din = (double *)work_ptr;
    work_ptr += sizeof(double) * num_samples;
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    layer->dout = (double *)work_ptr;
    work_ptr += sizeof(double) * num_samples;

    /* パラメータ領域確保 */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    layer->params = (double *)work_ptr;
    work_ptr += sizeof(double) * num_params;
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    layer->dparams = (double *)work_ptr;
    work_ptr += sizeof(double) * num_params;

    /* バッファオーバーランチェック */
    LPCNET_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    /* 確保した領域を0埋め */
    for (i = 0; i < layer->num_samples; i++) {
        layer->din[i] = 0.0f;
        layer->dout[i] = 0.0f;
    }
    for (i = 0; i < layer->num_params; i++) {
        layer->params[i] = 0.0f;
        layer->dparams[i] = 0.0f;
    }

    /* ひとまず1分割に設定 */
    layer->num_units = 1;

    return layer;
}

/* LPCネットレイヤー破棄 */
static void LPCNetLayer_Destroy(struct LPCNetLayer *layer)
{
    /* 特に何もしない */
    LPCNET_ASSERT(layer != NULL);
}

/* LPCネットレイヤーの順行伝播 */
static void LPCNetLayer_Forward(
        struct LPCNetLayer *layer, double *data, uint32_t num_samples)
{
    uint32_t unit, i, j;
    uint32_t nsmpls_per_unit, nparams_per_unit;

    LPCNET_ASSERT(layer != NULL);
    LPCNET_ASSERT(data != NULL);
    LPCNET_ASSERT(num_samples <= layer->num_samples);
    LPCNET_ASSERT(layer->num_units >= 1);

    /* 入力をコピー */
    memcpy(layer->din, data, sizeof(double) * num_samples);

    nsmpls_per_unit = num_samples / layer->num_units;
    nparams_per_unit = layer->num_params / layer->num_units;

    /* 残差計算 */
    for (unit = 0; unit < layer->num_units; unit++) {
        const double *pparams = &layer->params[unit * nparams_per_unit];
        const double *pdin = &layer->din[unit * nsmpls_per_unit];
        double *presidual = &data[unit * nsmpls_per_unit];
        double predict;
        /* 行列積として取り扱うため,
        * h[0]は最も古い入力, h[nparams-1]は直前のサンプルに対応させる
        * 一般的なFIRフィルタと係数順序が逆になるの注意 */
        /* 開始直後は入力ベクトルは0埋めされている */
        for (i = 0; i < nparams_per_unit; i++) {
            predict = 0.0f;
            for (j = 0; j < nparams_per_unit; j++) {
                if ((i + j) >= nparams_per_unit) {
                    predict += pparams[j] * pdin[i - nparams_per_unit + j];
                }
            }
            presidual[i] += predict;
        }
        for (; i < nsmpls_per_unit; i++) {
            predict = 0.0f;
            for (j = 0; j < nparams_per_unit; j++) {
                predict += pparams[j] * pdin[i - nparams_per_unit + j];
            }
            presidual[i] += predict;
        }
    }
}

/* LPCネットレイヤーの誤差逆伝播 */
static void LPCNetLayer_Backward(
        struct LPCNetLayer *layer, double *data, uint32_t num_samples)
{
    uint32_t unit, i, j;
    uint32_t nsmpls_per_unit, nparams_per_unit;

    LPCNET_ASSERT(layer != NULL);
    LPCNET_ASSERT(data != NULL);
    LPCNET_ASSERT(num_samples <= layer->num_samples);
    LPCNET_ASSERT(layer->num_units >= 1);

    /* 逆伝播信号をコピー */
    memcpy(layer->dout, data, sizeof(double) * num_samples);

    nsmpls_per_unit = num_samples / layer->num_units;
    nparams_per_unit = layer->num_params / layer->num_units;

    for (unit = 0; unit < layer->num_units; unit++) {
        const double *pin = &layer->din[unit * nsmpls_per_unit];
        const double *pout = &layer->dout[unit * nsmpls_per_unit];
        const double *pparams = &layer->params[unit * nparams_per_unit];
        double *pback = &data[unit * nsmpls_per_unit];
        double *pdparams = &layer->dparams[unit * nparams_per_unit];

        /* パラメータ勾配計算 */
        for (i = 0; i < nparams_per_unit; i++) {
            pdparams[i] = 0.0f;
            for (j = 0; j < (nsmpls_per_unit - nparams_per_unit + i); j++) {
                pdparams[i] += pin[j] * pout[nparams_per_unit - i + j];
            }
        }

        /* 逆伝播信号計算 */
        for (i = 0; i < (nsmpls_per_unit - nparams_per_unit); i++) {
            double back = 0.0f;
            for (j = 0; j < nparams_per_unit; j++) {
                back += pparams[j] * pout[nparams_per_unit + i - j];
            }
            /* 入力はパラメータ数だけ複製されているのでパラメータ数で割る */
            pback[i] += back / nparams_per_unit;
        }
        /* 端点 */
        for (; i < nsmpls_per_unit; i++) {
            double back = 0.0f;
            for (j = 0; j < nparams_per_unit; j++) {
                if ((nparams_per_unit + i - j) < nsmpls_per_unit) {
                    back += pparams[j] * pout[nparams_per_unit + i - j];
                }
            }
            pback[i] += back / nparams_per_unit;
        }
    }
}

/* 最適なユニット数とパラメータの探索・設定 */
static void LPCNetLayer_SetOptimalNumUnitsAndParameter(
        struct LPCNetLayer *layer, struct LPCCalculator *lpcc,
        const double *input, uint32_t num_samples, const uint32_t max_num_units)
{
    uint32_t unit, nunits;
    double min_loss = FLT_MAX;
    uint32_t tmp_best_nunits = 0;
    double params_buffer[LPCNET_MAX_PARAMS_PER_LAYER];
    double best_params[LPCNET_MAX_PARAMS_PER_LAYER];

    LPCNET_ASSERT(layer != NULL);
    LPCNET_ASSERT(lpcc != NULL);
    LPCNET_ASSERT(input != NULL);
    LPCNET_ASSERT(layer->num_params >= max_num_units);
    LPCNET_ASSERT(LPCNET_IS_POWERED_OF_2(max_num_units));

    for (nunits = 1; nunits <= max_num_units; nunits <<= 1) {
        const uint32_t nparams_per_unit = layer->num_params / nunits;
        const uint32_t nsmpls_per_unit = num_samples / nunits;
        double mean_loss = 0.0f;
        LPCNET_ASSERT(LPCNET_MAX_PARAMS_PER_LAYER >= nparams_per_unit);

        /* ユニット数で分割できなくなったら探索を打ち切る */
        if (((layer->num_params % nunits) != 0)
                || ((num_samples % nunits) != 0)) {
            break;
        }

        /* 各ユニット数における誤差を計算し、ベストなユニット数を探る */
        for (unit = 0; unit < nunits; unit++) {
            uint32_t smpl, k;
            const double *pinput = &input[unit * nsmpls_per_unit];
            double *pparams = &params_buffer[unit * nparams_per_unit];
            LPCApiResult ret;
            ret = LPCCalculator_CalculateLPCCoefficientsAF(lpcc,
                    pinput, nsmpls_per_unit, pparams, nparams_per_unit, 10);
            LPCNET_ASSERT(ret == LPC_APIRESULT_OK);
            /* その場で予測, 平均絶対値誤差を計算 */
            for (smpl = 0; smpl < nsmpls_per_unit; smpl++) {
                double predict = 0.0f;
                if (smpl < nparams_per_unit) {
                    for (k = 0; k < smpl; k++) {
                        predict += pparams[k] * pinput[smpl - k - 1];
                    }
                } else {
                    for (k = 0; k < nparams_per_unit; k++) {
                        predict += pparams[k] * pinput[smpl - k - 1];
                    } 
                }
                mean_loss += fabs(pinput[smpl] + predict);
            }
        }
        mean_loss /= num_samples;
        if (mean_loss < min_loss) {
            min_loss = mean_loss;
            tmp_best_nunits = nunits;
            memcpy(best_params, params_buffer, sizeof(double) * layer->num_params);
        }
    }

    /* 最適なユニット数とパラメータの設定 */
    LPCNET_ASSERT(tmp_best_nunits != 0);
    layer->num_units = tmp_best_nunits;

    {
        uint32_t i;
        const uint32_t nparams_per_unit = layer->num_params / layer->num_units;
        memcpy(layer->params, best_params, sizeof(double) * layer->num_params);
        for (unit = 0; unit < layer->num_units; unit++) {
            double *pparams = &layer->params[unit * nparams_per_unit];
            /* 順行伝播を行列演算で扱う都合上、パラメータ順序をリバース */
            for (i = 0; i < nparams_per_unit / 2; i++) {
                double tmp = pparams[i];
                pparams[i] = pparams[nparams_per_unit - i - 1];
                pparams[nparams_per_unit - i - 1] = tmp;
            }
        }
    }
}

/* LPCネット作成に必要なワークサイズの計算 */
int32_t LPCNet_CalculateWorkSize(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer)
{
    int32_t work_size;

    /* 引数チェック */
    if ((max_num_samples == 0)
            || (max_num_layers == 0)
            || (max_num_parameters_per_layer == 0)) {
        return -1;
    }

    work_size = sizeof(struct LPCNet) + LPCNET_MEMORY_ALIGNMENT;
    work_size += sizeof(struct LPCNetLayer *) * max_num_layers;
    work_size += max_num_layers * (size_t)LPCNetLayer_CalculateWorkSize(max_num_samples, max_num_parameters_per_layer);
    work_size += LPCCalculator_CalculateWorkSize(max_num_parameters_per_layer);
    work_size += (sizeof(double) * max_num_samples + LPCNET_MEMORY_ALIGNMENT);

    return work_size;
}

/* LPCネット作成 */
struct LPCNet *LPCNet_Create(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer, void *work, int32_t work_size)
{
    uint32_t l;
    struct LPCNet *net;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((max_num_samples == 0)
            || (max_num_layers == 0)
            || (max_num_parameters_per_layer == 0)
            || (work == NULL)
            || (work_size < LPCNet_CalculateWorkSize(max_num_samples, max_num_layers, max_num_parameters_per_layer))) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    net = (struct LPCNet *)work_ptr;
    work_ptr += sizeof(struct LPCNet);

    /* 構造体メンバ設定 */
    net->max_num_layers = (int32_t)max_num_layers;
    net->max_num_params = max_num_parameters_per_layer;
    net->max_num_samples = max_num_samples;
    net->num_layers = (int32_t)max_num_layers; /* ひとまず最大数で確保 */
    net->num_samples = max_num_samples; /* ひとまず最大数で確保 */

    /* LPCネットレイヤー作成 */
    {
        const int32_t layer_work_size = LPCNetLayer_CalculateWorkSize(max_num_samples, max_num_parameters_per_layer);

        /* ポインタ領域確保 */
        work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
        net->layers = (struct LPCNetLayer **)work_ptr;
        work_ptr += (sizeof(struct LPCNetLayer *) * max_num_layers);

        /* レイヤー領域確保 */
        net->layers_work = work_ptr;
        for (l = 0; l < max_num_layers; l++) {
            net->layers[l] = LPCNetLayer_Create(max_num_samples, max_num_parameters_per_layer, work_ptr, layer_work_size);
            work_ptr += layer_work_size;
        }
    }

    /* LPC係数計算ハンドル作成 */
    {
        const int32_t lpcc_work_size = LPCCalculator_CalculateWorkSize(max_num_parameters_per_layer);
        net->lpcc = LPCCalculator_Create(max_num_parameters_per_layer, work_ptr, lpcc_work_size);
        work_ptr += lpcc_work_size;
    }

    /* データバッファ領域確保 */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    net->data_buffer = (double *)work_ptr;
    work_ptr += sizeof(double) * max_num_samples;

    /* バッファオーバーランチェック */
    LPCNET_ASSERT(((uint8_t *)work - work_ptr) <= work_size);

    return net;
}

/* LPCネット破棄 */
void LPCNet_Destroy(struct LPCNet *net)
{
    if (net != NULL) {
        int32_t l;
        for (l = 0; l < net->num_layers; l++) {
            LPCNetLayer_Destroy(net->layers[l]);
        }
        LPCCalculator_Destroy(net->lpcc);
    }
}

/* 各層のパラメータ数を設定 */
void LPCNet_SetLayerStructure(struct LPCNet *net, uint32_t num_samples, uint32_t num_layers, const uint32_t *num_params_list)
{
    uint8_t *work_ptr;
    int32_t l;
    uint32_t max_num_params_per_layer;

    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(num_params_list != NULL);
    LPCNET_ASSERT(num_layers > 0);
    LPCNET_ASSERT((int32_t)num_layers <= net->max_num_layers);
    LPCNET_ASSERT(num_samples > 0);
    LPCNET_ASSERT(num_samples <= net->max_num_samples);

    /* 最大の層あたりパラメータ数のチェック */
    max_num_params_per_layer = 0;
    for (l = 0; l < (int32_t)num_layers; l++) {
        if (max_num_params_per_layer < num_params_list[l]) {
            max_num_params_per_layer = num_params_list[l];
        }
    }
    LPCNET_ASSERT(max_num_params_per_layer > 0);
    LPCNET_ASSERT(max_num_params_per_layer <= net->max_num_params);

    /* 現在確保しているレイヤーを破棄 */
    for (l = 0; l < net->num_layers; l++) {
        LPCNetLayer_Destroy(net->layers[l]);
    }

    /* 渡された配列パラメータ情報に従ってレイヤー作成 */
    work_ptr = (uint8_t *)net->layers_work;
    for (l = 0; l < (int32_t)num_layers; l++) {
        const int32_t work_size = LPCNetLayer_CalculateWorkSize(num_samples, num_params_list[l]);
        net->layers[l] = LPCNetLayer_Create(num_samples, num_params_list[l], work_ptr, work_size);
        LPCNET_ASSERT(net->layers[l] != NULL);
        work_ptr += work_size;
    }

    /* サンプル数/レイヤー数更新 */
    net->num_layers = (int32_t)num_layers;
    net->num_samples = num_samples;
}

/* ロス計算 同時に残差を計算にdataに書き込む */
double LPCNet_CalculateLoss(struct LPCNet *net, double *data, uint32_t num_samples)
{
    int32_t l;

    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(data != NULL);

    /* 順行伝播 */
    for (l = 0; l < net->num_layers; l++) {
        LPCNetLayer_Forward(net->layers[l], data, num_samples);
    }

    /* ロス計算 */
    return LPCL1Norm_Loss(data, num_samples);
}

/* 入力から勾配を計算（結果は内部変数にセット） */
static double LPCNet_CalculateGradient(
        struct LPCNet *net, double *data, uint32_t num_samples)
{
    int32_t l;
    double loss;

    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(data != NULL);

    /* 順行伝播 */
    loss = LPCNet_CalculateLoss(net, data, num_samples);

    /* 誤差勾配計算 */
    LPCL1Norm_Backward(data, num_samples);

    /* 誤差逆伝播 */
    for (l = net->num_layers - 1; l >= 0; l--) {
        LPCNetLayer_Backward(net->layers[l], data, num_samples);
    }

    return loss;
}

/* Levinson-Durbin法に基づく最適なユニット数とパラメータの設定 */
void LPCNet_SetUnitsAndParametersByLevinsonDurbin(
        struct LPCNet *net, const double *input, uint32_t num_samples)
{
    int32_t l;

    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(input != NULL);
    LPCNET_ASSERT(num_samples <= net->num_samples);

    memcpy(net->data_buffer, input, sizeof(double) * num_samples);
    for (l = 0; l < net->num_layers; l++) {
        LPCNetLayer_SetOptimalNumUnitsAndParameter(
                net->layers[l], net->lpcc, net->data_buffer, num_samples, net->layers[l]->num_params);
        LPCNetLayer_Forward(net->layers[l], net->data_buffer, num_samples);
    }
}

/* パラメータのクリア */
void LPCNet_ResetParameters(struct LPCNet *net)
{
    uint32_t l, i;

    for (l = 0; l < (uint32_t)net->num_layers; l++) {
        for (i = 0; i < net->layers[l]->num_params; i++) {
            net->layers[l]->params[i] = 0.0f;
        }
    }
}

/* 各レイヤーのユニット数取得 */
void LPCNet_GetLayerNumUnits(
        const struct LPCNet *net, uint32_t *num_units_buffer, const uint32_t buffer_size)
{
    int32_t l;

    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(num_units_buffer != NULL);
    LPCNET_ASSERT(buffer_size >= (uint32_t)net->num_layers);

    for (l = 0; l < net->num_layers; l++) {
        num_units_buffer[l] = net->layers[l]->num_units;
    }
}

/* パラメータ取得 */
void LPCNet_GetParameters(
        const struct LPCNet *net, double **params_buffer,
        const uint32_t buffer_num_layers, const uint32_t buffer_num_params_per_layer)
{
    int32_t l;

    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(params_buffer != NULL);
    LPCNET_ASSERT(buffer_num_layers >= (uint32_t)net->num_layers);

    for (l = 0; l < net->num_layers; l++) {
        uint32_t u, i;
        const struct LPCNetLayer *layer = net->layers[l];
        const uint32_t nparams_per_unit = layer->num_params / layer->num_units;
        LPCNET_ASSERT(params_buffer[l] != NULL);
        LPCNET_ASSERT(buffer_num_params_per_layer >= layer->num_params);
        /* 一旦バッファ領域にコピー */
        memcpy(params_buffer[l], layer->params, sizeof(double) * layer->num_params);
        /* ユニット毎にパラメータ順序をリバース */
        for (u = 0; u < layer->num_units; u++) {
            double *pparams = &params_buffer[l][u * nparams_per_unit];
            for (i = 0; i < nparams_per_unit / 2; i++) {
                double tmp = pparams[i];
                pparams[i] = pparams[nparams_per_unit - i - 1];
                pparams[nparams_per_unit - i - 1] = tmp;
            }
        }
    }
}

/* 入力データからサンプルあたりの推定符号長を求める */
double LPCNet_EstimateCodeLength(
        struct LPCNet *net,
        const double *data, uint32_t num_samples, uint32_t bits_per_sample)
{
    double tmp_length;
    LPCApiResult ret;

    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(data != NULL);

    /* TODO: 仮実装。1層目のパラメータのみを用いて推定 */
    ret = LPCCalculator_EstimateCodeLength(net->lpcc,
            data, num_samples, bits_per_sample, net->layers[0]->num_params, &tmp_length);
    LPCNET_ASSERT(ret == LPC_APIRESULT_OK);

    return tmp_length;
}

/* LPCネットトレーナー作成に必要なワークサイズ計算 */
int32_t LPCNetTrainer_CalculateWorkSize(uint32_t max_num_layers, uint32_t max_num_params_per_layer)
{
    int32_t work_size;

    /* 引数チェック */
    if ((max_num_layers == 0) || (max_num_params_per_layer == 0)) {
        return -1;
    }

    work_size = sizeof(struct LPCNetTrainer) + LPCNET_MEMORY_ALIGNMENT;

    /* For momentum */
    work_size += sizeof(double *) * max_num_layers + LPCNET_MEMORY_ALIGNMENT;
    work_size += max_num_layers * max_num_params_per_layer * sizeof(double) + LPCNET_MEMORY_ALIGNMENT;

#if 0
    /* For AdaGrad */
    work_size += sizeof(double *) * max_num_layers + LPCNET_MEMORY_ALIGNMENT;
    work_size += max_num_layers * max_num_params_per_layer * sizeof(double) + LPCNET_MEMORY_ALIGNMENT;

    /* For Adam */
    work_size += 2 * (sizeof(double *) * max_num_layers + LPCNET_MEMORY_ALIGNMENT);
    work_size += 2 * (max_num_layers * max_num_params_per_layer * sizeof(double) + LPCNET_MEMORY_ALIGNMENT);
#endif

    return work_size;
}

/* LPCネットトレーナー作成 */
struct LPCNetTrainer *LPCNetTrainer_Create(
        uint32_t max_num_layers, uint32_t max_num_params_per_layer, void *work, int32_t work_size)
{
    uint32_t l;
    struct LPCNetTrainer *trainer;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((max_num_layers == 0) || (max_num_params_per_layer == 0) || (work == NULL)
            || (work_size < LPCNetTrainer_CalculateWorkSize(max_num_layers, max_num_params_per_layer))) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    trainer = (struct LPCNetTrainer *)work_ptr;
    work_ptr += sizeof(struct LPCNetTrainer);

    trainer->max_num_layers = max_num_layers;
    trainer->max_num_params_per_layer = max_num_params_per_layer;

    /* For momentum */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    trainer->momentum = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
        trainer->momentum[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }

#if 0
    /* For AdaGrad */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    trainer->grad_rs = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
        trainer->grad_rs[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }

    /* For Adam */
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    trainer->m = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
        trainer->m[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }
    work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
    trainer->v = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LPCNET_ROUNDUP((uintptr_t)work_ptr, LPCNET_MEMORY_ALIGNMENT);
        trainer->v[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }
#endif

    /* バッファオーバーランチェック */
    LPCNET_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    return trainer;
}

/* LPCネットトレーナー破棄 */
void LPCNetTrainer_Destroy(struct LPCNetTrainer *trainer)
{
    /* 特に何もしない */
    LPCNET_ASSERT(trainer != NULL);
}

/* 学習 */
void LPCNetTrainer_Train(struct LPCNetTrainer *trainer,
        struct LPCNet *net, const double *input, uint32_t num_samples,
        uint32_t max_num_iteration, double learning_rate, double loss_epsilon)
{
    uint32_t itr, i;
    int32_t l;
    double loss, prev_loss = FLT_MAX;

    LPCNET_ASSERT(trainer != NULL);
    LPCNET_ASSERT(net != NULL);
    LPCNET_ASSERT(input != NULL);
    LPCNET_ASSERT(num_samples <= net->num_samples);
    LPCNET_ASSERT(loss_epsilon >= 0.0f);

    /* モーメンタムを初期化 */
    for (l = 0; l < net->num_layers; l++) {
        for (i = 0; i < net->layers[l]->num_params; i++) {
            trainer->momentum[l][i] = 0.0f;
#if 0
            trainer->grad_rs[l][i] = 0.0f;
            trainer->m[l][i] = 0.0f;
            trainer->v[l][i] = 0.0f;
#endif
        }
    }

    /* モーメンタムのハイパラ設定 */
    trainer->momentum_alpha = 0.8f;
#if 0
    /* Adamのハイパラ設定 */
    trainer->beta1 = 0.9f;
    trainer->beta2 = 0.999f;
#endif

    /* 学習繰り返し */
    for (itr = 0; itr < max_num_iteration; itr++) {
        memcpy(net->data_buffer, input, sizeof(double) * num_samples);
        loss = LPCNet_CalculateGradient(net, net->data_buffer, num_samples);
        for (l = 0; l < net->num_layers; l++) {
            struct LPCNetLayer *layer = net->layers[l];
            for (i = 0; i < layer->num_params; i++) {
#if 1
                /* Momentum */
                trainer->momentum[l][i] = trainer->momentum_alpha * trainer->momentum[l][i] + learning_rate * layer->dparams[i];
                layer->params[i] -= trainer->momentum[l][i];
#endif
#if 0
                /* AdaGrad */
                trainer->grad_rs[l][i] += pow(layer->dparams[i], 2.0f);
                layer->params[i] -= learning_rate * layer->dparams[i] / (sqrt(trainer->grad_rs[l][i]) + 1e-8);
#endif
#if 0
                /* Adam */
                {
                    const double lr = learning_rate * sqrt(1.0f - pow(trainer->beta2, (itr + 1))) / (1.0f - pow(trainer->beta1, (itr + 1)));
                    trainer->m[l][i] = trainer->beta1 * trainer->m[l][i] + (1.0f - trainer->beta1) * layer->dparams[i];
                    trainer->v[l][i] = trainer->beta2 * trainer->v[l][i] + (1.0f - trainer->beta2) * pow(layer->dparams[i], 2);
                    layer->params[i] -= lr * trainer->m[l][i] / (sqrt(trainer->v[l][i]) + 1e-8);
                }
#endif
            }
        }
        /* 収束判定 */
        if (fabs(loss - prev_loss) < loss_epsilon) {
            break;
        }
        prev_loss = loss;
    }
}
