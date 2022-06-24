#include "linne_lpc_predict.h"

#include <string.h>
#include "linne_internal.h"

/* LPC係数により予測/誤差出力 */
void LINNELPC_Predict(
    const int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, int32_t *residual, uint32_t coef_rshift, int32_t is_first_unit)
{
    int32_t predict;
    uint32_t smpl, ord;
    const int32_t half = 1 << (coef_rshift - 1); /* 固定小数の0.5 */

    /* 引数チェック */
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(coef != NULL);
    LINNE_ASSERT(residual != NULL);
    LINNE_ASSERT(coef_rshift != 0);

    memcpy(residual, data, sizeof(int32_t) * num_samples);

    smpl = 0;
    if (is_first_unit) {
        /* LPC係数による予測 */
        for (smpl = 1; smpl < coef_order; smpl++) {
            predict = half;
            for (ord = 0; ord < smpl; ord++) {
                predict += (coef[coef_order - smpl + ord] * data[ord]);
            }
            residual[smpl] += (predict >> coef_rshift);
        }
    }
    for (; smpl < num_samples; smpl++) {
        predict = half;
        for (ord = 0; ord < coef_order; ord++) {
            predict += (coef[ord] * data[(int32_t)(smpl - coef_order + ord)]);
        }
        residual[smpl] += (predict >> coef_rshift);
    }
}
