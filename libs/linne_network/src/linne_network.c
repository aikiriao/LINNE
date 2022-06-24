#include "linne_network.h"
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <float.h>
#include "lpc.h"
#include "linne_internal.h"
#include "linne_utility.h"

/* LINNEネットを構成するレイヤー */
struct LINNENetworkLayer {
    double *din; /* 入力信号バッファ */
    double *dout; /* 逆伝播信号バッファ */
    double *params; /* パラメータ（LPC係数） */
    double *dparams; /* パラメータ勾配 */
    uint32_t num_samples; /* 入力サンプル数 */
    uint32_t num_params; /* レイヤー内の全パラメータ数 */
    uint32_t num_units; /* レイヤー内のユニット数 */
};

/* LINNEネット */
struct LINNENetwork {
    struct LINNENetworkLayer **layers; /* レイヤー配列 */
    void *layers_work; /* レイヤー配列の先頭領域 */
    uint32_t max_num_samples; /* 最大サンプル数 */
    int32_t max_num_layers; /* 最大レイヤー（層）数 */
    uint32_t max_num_params; /* 最大レイヤーあたりパラメータ数 */
    struct LPCCalculator *lpcc; /* LPC係数計算ハンドル */
    double *data_buffer; /* 入力データバッファ */
    uint32_t num_samples; /* 入力サンプル数 */
    int32_t num_layers; /* レイヤー数 */
};

/* LINNEネットトレーナー */
struct LINNENetworkTrainer {
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
static double LINNEL1Norm_Loss(const double *data, uint32_t num_samples)
{
    uint32_t smpl;
    double norm = 0.0f;

    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(num_samples > 0);

    for (smpl = 0; smpl < num_samples; smpl++) {
        norm += fabs(data[smpl]);
    }

    return norm / num_samples;
}

/* L1ノルムレイヤーの誤差逆伝播 */
static void LINNEL1Norm_Backward(double *data, uint32_t num_samples)
{
    uint32_t smpl;

    LINNE_ASSERT(data != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        data[smpl] = (double)LINNEUTILITY_SIGN(data[smpl]) / num_samples;
    }
}

/* LINNEネットレイヤー作成に必要なワークサイズ計算 */
static int32_t LINNENetworkLayer_CalculateWorkSize(uint32_t num_samples, uint32_t num_params)
{
    int32_t work_size;

    /* 1サンプル遅れの畳込みを行うため、サンプル数はパラメータ数よりも大きいことを要求 */
    if (num_samples <= num_params) {
        return -1;
    }

    work_size = sizeof(struct LINNENetworkLayer) + LINNE_MEMORY_ALIGNMENT;
    work_size += 2 * (sizeof(double) * num_samples + LINNE_MEMORY_ALIGNMENT);
    work_size += 2 * (sizeof(double) * num_params + LINNE_MEMORY_ALIGNMENT);

    return work_size;
}

/* LINNEネットレイヤー作成 */
static struct LINNENetworkLayer *LINNENetworkLayer_Create(
        uint32_t num_samples, uint32_t num_params, void *work, int32_t work_size)
{
    uint32_t i;
    struct LINNENetworkLayer *layer;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((work == NULL)
            || (work_size < LINNENetworkLayer_CalculateWorkSize(num_samples, num_params))) {
        return NULL;
    }

    /* 1サンプル遅れの畳込みを行うため、サンプル数はパラメータ数よりも大きいことを要求 */
    if (num_samples <= num_params) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    layer = (struct LINNENetworkLayer *)work_ptr;
    work_ptr += sizeof(struct LINNENetworkLayer);
    layer->num_samples = num_samples;
    layer->num_params = num_params;

    /* 入出力バッファ領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    layer->din = (double *)work_ptr;
    work_ptr += sizeof(double) * num_samples;
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    layer->dout = (double *)work_ptr;
    work_ptr += sizeof(double) * num_samples;

    /* パラメータ領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    layer->params = (double *)work_ptr;
    work_ptr += sizeof(double) * num_params;
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    layer->dparams = (double *)work_ptr;
    work_ptr += sizeof(double) * num_params;

    /* バッファオーバーランチェック */
    LINNE_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

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

/* LINNEネットレイヤー破棄 */
static void LINNENetworkLayer_Destroy(struct LINNENetworkLayer *layer)
{
    /* 特に何もしない */
    LINNE_ASSERT(layer != NULL);
}

/* LINNEネットレイヤーの順行伝播 */
static void LINNENetworkLayer_Forward(
        struct LINNENetworkLayer *layer, double *data, uint32_t num_samples)
{
    uint32_t unit, i, j;
    uint32_t nsmpls_per_unit, nparams_per_unit;

    LINNE_ASSERT(layer != NULL);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(num_samples <= layer->num_samples);
    LINNE_ASSERT(layer->num_units >= 1);

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
        /* 開始直後は入力ベクトルは0埋めされていると考える */
        for (i = 1; i < nparams_per_unit; i++) {
            predict = 0.0f;
            for (j = 0; j < i; j++) {
                predict += pparams[nparams_per_unit - i + j] * pdin[j];
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

/* LINNEネットレイヤーの誤差逆伝播 */
static void LINNENetworkLayer_Backward(
        struct LINNENetworkLayer *layer, double *data, uint32_t num_samples)
{
    uint32_t unit, i, j;
    uint32_t nsmpls_per_unit, nparams_per_unit;

    LINNE_ASSERT(layer != NULL);
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(num_samples <= layer->num_samples);
    LINNE_ASSERT(layer->num_units >= 1);

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

/* 最適なユニット数の探索 */
static void LINNENetworkLayer_SearchOptimalNumUnits(
        struct LINNENetworkLayer *layer, struct LPCCalculator *lpcc,
        const double *input, uint32_t num_samples, const uint32_t max_num_units,
        double regular_term, uint32_t *best_num_units)
{
    uint32_t unit, nunits;
    double min_loss = FLT_MAX;
    uint32_t tmp_best_nunits = 0;

    LINNE_ASSERT(layer != NULL);
    LINNE_ASSERT(lpcc != NULL);
    LINNE_ASSERT(input != NULL);
    LINNE_ASSERT(best_num_units != NULL);
    LINNE_ASSERT(layer->num_params >= max_num_units);
    LINNE_ASSERT(LINNEUTILITY_IS_POWERED_OF_2(max_num_units));

    for (nunits = 1; nunits <= max_num_units; nunits <<= 1) {
        const uint32_t nparams_per_unit = layer->num_params / nunits;
        const uint32_t nsmpls_per_unit = num_samples / nunits;
        double mean_loss = 0.0f;
        LINNE_ASSERT(layer->num_params >= nparams_per_unit);

        /* ユニット数で分割できない場合はスキップ */
        if (((layer->num_params % nunits) != 0)
                || ((num_samples % nunits) != 0)) {
            continue;
        }

        /* 各ユニット数における誤差を計算し、ベストなユニット数を探る */
        for (unit = 0; unit < nunits; unit++) {
            uint32_t smpl, k;
            const double *pinput = &input[unit * nsmpls_per_unit];
            double *pparams = &layer->params[unit * nparams_per_unit];
            LPCApiResult ret;
            double residual;

            /* 係数計算 */
            ret = LPCCalculator_CalculateLPCCoefficientsAF(lpcc,
                pinput, nsmpls_per_unit, pparams, nparams_per_unit,
                LINNE_NUM_AF_METHOD_ITERATION_DETERMINEUNIT, LPC_WINDOWTYPE_WELCH, regular_term);
            LINNE_ASSERT(ret == LPC_APIRESULT_OK);

            /* 行列（畳み込み）演算でインデックスが増える方向にしたい都合上、
            * パラメータ順序を反転 */
            for (k = 0; k < nparams_per_unit / 2; k++) {
                double tmp = pparams[k];
                pparams[k] = pparams[nparams_per_unit - k - 1];
                pparams[nparams_per_unit - k - 1] = tmp;
            }

            /* その場で予測, 平均絶対値誤差を計算 */
            smpl = 0;
            if (unit == 0) {
                for (smpl = 1; smpl < nparams_per_unit; smpl++) {
                    residual = pinput[smpl];
                    for (k = 0; k < smpl; k++) {
                        residual += pparams[nparams_per_unit - smpl + k] * pinput[k];
                    }
                    mean_loss += LINNEUTILITY_ABS(residual);
                }
            }
            for (; smpl < nsmpls_per_unit; smpl++) {
                residual = pinput[smpl];
                for (k = 0; k < nparams_per_unit; k++) {
                    residual += pparams[k] * pinput[(int32_t)(smpl - nparams_per_unit + k)];
                }
                mean_loss += LINNEUTILITY_ABS(residual);
            }
        }
        mean_loss /= num_samples;
        if (mean_loss < min_loss) {
            min_loss = mean_loss;
            tmp_best_nunits = nunits;
        }
    }

    /* 最適なユニット数の設定 */
    LINNE_ASSERT(tmp_best_nunits != 0);
    (*best_num_units) = tmp_best_nunits;
}

/* パラメータの設定 */
static void LINNENetworkLayer_SetParameter(
    struct LINNENetworkLayer *layer, struct LPCCalculator *lpcc,
    const double *input, uint32_t num_samples, uint32_t num_af_iterations, double regular_term)
{
    uint32_t i, unit;
    const uint32_t nparams_per_unit = layer->num_params / layer->num_units;
    const uint32_t nsmpls_per_unit = num_samples / layer->num_units;

    for (unit = 0; unit < layer->num_units; unit++) {
        const double *pinput = &input[unit * nsmpls_per_unit];
        double *pparams = &layer->params[unit * nparams_per_unit];
        LPCApiResult ret;

        /* 係数計算 */
        ret = LPCCalculator_CalculateLPCCoefficientsAF(lpcc,
            pinput, nsmpls_per_unit, pparams, nparams_per_unit, num_af_iterations, LPC_WINDOWTYPE_WELCH, regular_term);
        LINNE_ASSERT(ret == LPC_APIRESULT_OK);

        /* 行列（畳み込み）演算でインデックスが増える方向にしたい都合上、
        * パラメータ順序を変転 */
        for (i = 0; i < nparams_per_unit / 2; i++) {
            double tmp = pparams[i];
            pparams[i] = pparams[nparams_per_unit - i - 1];
            pparams[nparams_per_unit - i - 1] = tmp;
        }
    }
}

/* LINNEネット作成に必要なワークサイズの計算 */
int32_t LINNENetwork_CalculateWorkSize(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer)
{
    int32_t work_size;
    struct LPCCalculatorConfig lpcconfig;

    /* 引数チェック */
    if ((max_num_samples == 0)
            || (max_num_layers == 0)
            || (max_num_parameters_per_layer == 0)) {
        return -1;
    }

    /* 1サンプル遅れの畳込みを行うため、サンプル数はパラメータ数よりも大きいことを要求 */
    if (max_num_samples <= max_num_parameters_per_layer) {
        return -1;
    }

    lpcconfig.max_order = max_num_parameters_per_layer;
    lpcconfig.max_num_samples = max_num_samples;

    work_size = sizeof(struct LINNENetwork) + LINNE_MEMORY_ALIGNMENT;
    work_size += sizeof(struct LINNENetworkLayer *) * max_num_layers;
    work_size += max_num_layers * (size_t)LINNENetworkLayer_CalculateWorkSize(max_num_samples, max_num_parameters_per_layer);
    work_size += LPCCalculator_CalculateWorkSize(&lpcconfig);
    work_size += (sizeof(double) * max_num_samples + LINNE_MEMORY_ALIGNMENT);

    return work_size;
}

/* LINNEネット作成 */
struct LINNENetwork *LINNENetwork_Create(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer, void *work, int32_t work_size)
{
    uint32_t l;
    struct LINNENetwork *net;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((max_num_samples == 0)
            || (max_num_layers == 0)
            || (max_num_parameters_per_layer == 0)
            || (work == NULL)
            || (work_size < LINNENetwork_CalculateWorkSize(max_num_samples, max_num_layers, max_num_parameters_per_layer))) {
        return NULL;
    }

    /* 1サンプル遅れの畳込みを行うため、サンプル数はパラメータ数よりも大きいことを要求 */
    if (max_num_samples <= max_num_parameters_per_layer) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    net = (struct LINNENetwork *)work_ptr;
    work_ptr += sizeof(struct LINNENetwork);

    /* 構造体メンバ設定 */
    net->max_num_layers = (int32_t)max_num_layers;
    net->max_num_params = max_num_parameters_per_layer;
    net->max_num_samples = max_num_samples;
    net->num_layers = (int32_t)max_num_layers; /* ひとまず最大数で確保 */
    net->num_samples = max_num_samples; /* ひとまず最大数で確保 */

    /* LINNEネットレイヤー作成 */
    {
        const int32_t layer_work_size = LINNENetworkLayer_CalculateWorkSize(max_num_samples, max_num_parameters_per_layer);

        /* ポインタ領域確保 */
        work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
        net->layers = (struct LINNENetworkLayer **)work_ptr;
        work_ptr += (sizeof(struct LINNENetworkLayer *) * max_num_layers);

        /* レイヤー領域確保 */
        net->layers_work = work_ptr;
        for (l = 0; l < max_num_layers; l++) {
            net->layers[l] = LINNENetworkLayer_Create(max_num_samples, max_num_parameters_per_layer, work_ptr, layer_work_size);
            work_ptr += layer_work_size;
        }
    }

    /* LPC係数計算ハンドル作成 */
    {
        int32_t lpcc_work_size;
        struct LPCCalculatorConfig lpcconfig;

        lpcconfig.max_order = max_num_parameters_per_layer;
        lpcconfig.max_num_samples = max_num_samples;
        lpcc_work_size  = LPCCalculator_CalculateWorkSize(&lpcconfig);
        net->lpcc = LPCCalculator_Create(&lpcconfig, work_ptr, lpcc_work_size);
        work_ptr += lpcc_work_size;
    }

    /* データバッファ領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    net->data_buffer = (double *)work_ptr;
    work_ptr += sizeof(double) * max_num_samples;

    /* バッファオーバーランチェック */
    LINNE_ASSERT(((uint8_t *)work - work_ptr) <= work_size);

    return net;
}

/* LPCネット破棄 */
void LINNENetwork_Destroy(struct LINNENetwork *net)
{
    if (net != NULL) {
        int32_t l;
        for (l = 0; l < net->num_layers; l++) {
            LINNENetworkLayer_Destroy(net->layers[l]);
        }
        LPCCalculator_Destroy(net->lpcc);
    }
}

/* 各層のパラメータ数を設定 */
void LINNENetwork_SetLayerStructure(struct LINNENetwork *net, uint32_t num_samples, uint32_t num_layers, const uint32_t *num_params_list)
{
    uint8_t *work_ptr;
    int32_t l;
    uint32_t max_num_params_per_layer;

    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(num_params_list != NULL);
    LINNE_ASSERT(num_layers > 0);
    LINNE_ASSERT((int32_t)num_layers <= net->max_num_layers);
    LINNE_ASSERT(num_samples > 0);
    LINNE_ASSERT(num_samples <= net->max_num_samples);

    /* 最大の層あたりパラメータ数のチェック */
    max_num_params_per_layer = 0;
    for (l = 0; l < (int32_t)num_layers; l++) {
        if (max_num_params_per_layer < num_params_list[l]) {
            max_num_params_per_layer = num_params_list[l];
        }
    }
    LINNE_ASSERT(max_num_params_per_layer > 0);
    LINNE_ASSERT(max_num_params_per_layer <= net->max_num_params);

    /* 現在確保しているレイヤーを破棄 */
    for (l = 0; l < net->num_layers; l++) {
        LINNENetworkLayer_Destroy(net->layers[l]);
    }

    /* 渡された配列パラメータ情報に従ってレイヤー作成 */
    work_ptr = (uint8_t *)net->layers_work;
    for (l = 0; l < (int32_t)num_layers; l++) {
        const int32_t work_size = LINNENetworkLayer_CalculateWorkSize(num_samples, num_params_list[l]);
        net->layers[l] = LINNENetworkLayer_Create(num_samples, num_params_list[l], work_ptr, work_size);
        LINNE_ASSERT(net->layers[l] != NULL);
        work_ptr += work_size;
    }

    /* サンプル数/レイヤー数更新 */
    net->num_layers = (int32_t)num_layers;
    net->num_samples = num_samples;
}

/* ロス計算 同時に残差を計算にdataに書き込む */
double LINNENetwork_CalculateLoss(struct LINNENetwork *net, double *data, uint32_t num_samples)
{
    int32_t l;

    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(data != NULL);

    /* 順行伝播 */
    for (l = 0; l < net->num_layers; l++) {
        LINNENetworkLayer_Forward(net->layers[l], data, num_samples);
    }

    /* ロス計算 */
    return LINNEL1Norm_Loss(data, num_samples);
}

/* 入力から勾配を計算（結果は内部変数にセット） */
static double LINNENetwork_CalculateGradient(
        struct LINNENetwork *net, double *data, uint32_t num_samples)
{
    int32_t l;
    double loss;

    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(data != NULL);

    /* 順行伝播 */
    loss = LINNENetwork_CalculateLoss(net, data, num_samples);

    /* 誤差勾配計算 */
    LINNEL1Norm_Backward(data, num_samples);

    /* 誤差逆伝播 */
    for (l = net->num_layers - 1; l >= 0; l--) {
        LINNENetworkLayer_Backward(net->layers[l], data, num_samples);
    }

    return loss;
}

/* 最適なユニット数の探索と設定 ロス計算を含む */
static double LINNENetwork_SearchSetUnitsAndParameters(
    struct LINNENetwork *net, const double *input, uint32_t num_samples, uint32_t num_af_iterations, double regular_term)
{
    int32_t l;
    const uint32_t max_num_units = 1UL << ((1UL << LINNE_LOG2_NUM_UNITS_BITWIDTH) - 1);

    memcpy(net->data_buffer, input, sizeof(double) * num_samples);
    for (l = 0; l < net->num_layers; l++) {
        uint32_t best_num_units;
        struct LINNENetworkLayer* layer = net->layers[l];
        LINNENetworkLayer_SearchOptimalNumUnits(
            layer, net->lpcc, net->data_buffer, num_samples,
            LINNEUTILITY_MIN(max_num_units, layer->num_params), regular_term, &best_num_units);
        layer->num_units = best_num_units;
        LINNENetworkLayer_SetParameter(layer, net->lpcc, net->data_buffer, num_samples,
            num_af_iterations, regular_term);
        LINNENetworkLayer_Forward(layer, net->data_buffer, num_samples);
    }

    return LINNEL1Norm_Loss(net->data_buffer, num_samples);
}

/* Levinson-Durbin法に基づく最適なユニット数・パラメータの設定 */
void LINNENetwork_SetUnitsAndParameters(
        struct LINNENetwork *net, const double *input, uint32_t num_samples,
        uint32_t num_afmethod_iterations, const double *regular_term_list, uint32_t regular_term_list_size)
{
    uint32_t i, best_i;
    double min_loss;

    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(input != NULL);
    LINNE_ASSERT(regular_term_list != NULL);
    LINNE_ASSERT(regular_term_list_size > 0);
    LINNE_ASSERT(num_samples <= net->num_samples);

    min_loss = FLT_MAX;
    for (i = 0; i < regular_term_list_size; i++) {
        double loss = LINNENetwork_SearchSetUnitsAndParameters(net,
            input, num_samples, LINNE_NUM_AF_METHOD_ITERATION_DETERMINEUNIT, regular_term_list[i]);
        if (loss < min_loss) {
            min_loss = loss;
            best_i = i;
        }
    }

    (void)LINNENetwork_SearchSetUnitsAndParameters(net,
        input, num_samples, num_afmethod_iterations, regular_term_list[best_i]);
}

/* パラメータのクリア */
void LINNENetwork_ResetParameters(struct LINNENetwork *net)
{
    uint32_t l, i;

    for (l = 0; l < (uint32_t)net->num_layers; l++) {
        for (i = 0; i < net->layers[l]->num_params; i++) {
            net->layers[l]->params[i] = 0.0f;
        }
    }
}

/* 各レイヤーのユニット数取得 */
void LINNENetwork_GetLayerNumUnits(
        const struct LINNENetwork *net, uint32_t *num_units_buffer, const uint32_t buffer_size)
{
    int32_t l;

    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(num_units_buffer != NULL);
    LINNE_ASSERT(buffer_size >= (uint32_t)net->num_layers);

    for (l = 0; l < net->num_layers; l++) {
        num_units_buffer[l] = net->layers[l]->num_units;
    }
}

/* パラメータ取得 */
void LINNENetwork_GetParameters(
        const struct LINNENetwork *net, double **params_buffer,
        const uint32_t buffer_num_layers, const uint32_t buffer_num_params_per_layer)
{
    int32_t l;

    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(params_buffer != NULL);
    LINNE_ASSERT(buffer_num_layers >= (uint32_t)net->num_layers);

    for (l = 0; l < net->num_layers; l++) {
        const struct LINNENetworkLayer *layer = net->layers[l];
        LINNE_ASSERT(params_buffer[l] != NULL);
        LINNE_ASSERT(buffer_num_params_per_layer >= layer->num_params);
        /* バッファ領域にコピー */
        memcpy(params_buffer[l], layer->params, sizeof(double) * layer->num_params);
    }
}

/* 入力データからサンプルあたりの推定符号長を求める */
double LINNENetwork_EstimateCodeLength(
        struct LINNENetwork *net,
        const double *data, uint32_t num_samples, uint32_t bits_per_sample)
{
    double tmp_length;
    LPCApiResult ret;

    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(data != NULL);

    /* TODO: 仮実装。1層目のパラメータのみを用いて推定 */
    ret = LPCCalculator_EstimateCodeLength(net->lpcc,
            data, num_samples, bits_per_sample, net->layers[0]->num_params, &tmp_length, LPC_WINDOWTYPE_SIN);
    LINNE_ASSERT(ret == LPC_APIRESULT_OK);

    return tmp_length;
}

/* LINNEネットトレーナー作成に必要なワークサイズ計算 */
int32_t LINNENetworkTrainer_CalculateWorkSize(uint32_t max_num_layers, uint32_t max_num_params_per_layer)
{
    int32_t work_size;

    /* 引数チェック */
    if ((max_num_layers == 0) || (max_num_params_per_layer == 0)) {
        return -1;
    }

    work_size = sizeof(struct LINNENetworkTrainer) + LINNE_MEMORY_ALIGNMENT;

    /* For momentum */
    work_size += sizeof(double *) * max_num_layers + LINNE_MEMORY_ALIGNMENT;
    work_size += max_num_layers * max_num_params_per_layer * sizeof(double) + LINNE_MEMORY_ALIGNMENT;

#if 0
    /* For AdaGrad */
    work_size += sizeof(double *) * max_num_layers + LINNE_MEMORY_ALIGNMENT;
    work_size += max_num_layers * max_num_params_per_layer * sizeof(double) + LINNE_MEMORY_ALIGNMENT;

    /* For Adam */
    work_size += 2 * (sizeof(double *) * max_num_layers + LINNE_MEMORY_ALIGNMENT);
    work_size += 2 * (max_num_layers * max_num_params_per_layer * sizeof(double) + LINNE_MEMORY_ALIGNMENT);
#endif

    return work_size;
}

/* LINNEネットトレーナー作成 */
struct LINNENetworkTrainer *LINNENetworkTrainer_Create(
        uint32_t max_num_layers, uint32_t max_num_params_per_layer, void *work, int32_t work_size)
{
    uint32_t l;
    struct LINNENetworkTrainer *trainer;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((max_num_layers == 0) || (max_num_params_per_layer == 0) || (work == NULL)
            || (work_size < LINNENetworkTrainer_CalculateWorkSize(max_num_layers, max_num_params_per_layer))) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    trainer = (struct LINNENetworkTrainer *)work_ptr;
    work_ptr += sizeof(struct LINNENetworkTrainer);

    trainer->max_num_layers = max_num_layers;
    trainer->max_num_params_per_layer = max_num_params_per_layer;

    /* For momentum */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    trainer->momentum = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
        trainer->momentum[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }

#if 0
    /* For AdaGrad */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    trainer->grad_rs = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
        trainer->grad_rs[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }

    /* For Adam */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    trainer->m = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
        trainer->m[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
    trainer->v = (double **)work_ptr;
    work_ptr += sizeof(double *) * max_num_layers;
    for (l = 0; l < max_num_layers; l++) {
        work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);
        trainer->v[l] = (double *)work_ptr;
        work_ptr += sizeof(double) * max_num_params_per_layer;
    }
#endif

    /* バッファオーバーランチェック */
    LINNE_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    return trainer;
}

/* LINNEネットトレーナー破棄 */
void LINNENetworkTrainer_Destroy(struct LINNENetworkTrainer *trainer)
{
    /* 特に何もしない */
    LINNE_ASSERT(trainer != NULL);
}

/* 学習 */
void LINNENetworkTrainer_Train(struct LINNENetworkTrainer *trainer,
        struct LINNENetwork *net, const double *input, uint32_t num_samples,
        uint32_t max_num_iteration, double learning_rate, double loss_epsilon)
{
    uint32_t itr, i;
    int32_t l;
    double loss, prev_loss = FLT_MAX;

    LINNE_ASSERT(trainer != NULL);
    LINNE_ASSERT(net != NULL);
    LINNE_ASSERT(input != NULL);
    LINNE_ASSERT(num_samples <= net->num_samples);
    LINNE_ASSERT(loss_epsilon >= 0.0f);

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
        loss = LINNENetwork_CalculateGradient(net, net->data_buffer, num_samples);
        for (l = 0; l < net->num_layers; l++) {
            struct LINNENetworkLayer *layer = net->layers[l];
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
