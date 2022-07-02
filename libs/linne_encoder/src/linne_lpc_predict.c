#include "linne_lpc_predict.h"

#include <string.h>
#include "linne_internal.h"

/* LPC係数により予測/誤差出力 */
void LINNELPC_Predict(
    const int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, int32_t *residual, uint32_t coef_rshift, uint32_t num_units)
{
    uint32_t u, smpl, ord;
    int32_t predict;
    const int32_t half = 1 << (coef_rshift - 1); /* 固定小数の0.5 */
    const uint32_t nparams_per_unit = coef_order / num_units;
    /* 補足: num_samplesはnunitsで割り切れなくてもよい 剰余分の末尾サンプルは予測しない */
    const uint32_t nsmpls_per_unit = num_samples / num_units;

    /* 引数チェック */
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(coef != NULL);
    LINNE_ASSERT(residual != NULL);

    memcpy(residual, data, sizeof(int32_t) * num_samples);

    /* 予測 */
    for (u = 0; u < num_units; u++) {
        const int32_t *pinput = &data[u * nsmpls_per_unit];
        int32_t *poutput = &residual[u * nsmpls_per_unit];
        const int32_t *pcoef = &coef[u * nparams_per_unit];
        for (smpl = 0; smpl < nsmpls_per_unit - nparams_per_unit; smpl++) {
            predict = half;
            for (ord = 0; ord < nparams_per_unit; ord++) {
                predict += (pcoef[ord] * pinput[smpl + ord]);
            }
            poutput[smpl + ord] += (predict >> coef_rshift);
        }
    }
}
