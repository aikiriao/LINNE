#include "linne_lpc_synthesize.h"

#include <string.h>
#include "linne_internal.h"
#include "linne_utility.h"

/* LPC係数により合成(in-place) */
void LINNELPC_Synthesize(
    int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift, uint32_t num_units)
{
    uint32_t smpl, ord, u;
    const int32_t half = 1 << (coef_rshift - 1); /* 固定小数の0.5 */
    const uint32_t nparams_per_unit = coef_order / num_units;
    const uint32_t nsmpls_per_unit = num_samples / num_units;

    /* 引数チェック */
    LINNE_ASSERT(data != NULL);
    LINNE_ASSERT(coef != NULL);

    /* ユニット数は2の冪であることを要求 */
    LINNE_ASSERT(num_units > 0);
    LINNE_ASSERT(LINNEUTILITY_IS_POWERED_OF_2(num_units));

    if (num_units == 1) {
        int32_t predict;
        for (smpl = 0; smpl < nsmpls_per_unit - nparams_per_unit; smpl++) {
            predict = half;
            for (ord = 0; ord < nparams_per_unit; ord++) {
                predict += (coef[ord] * data[smpl + ord]);
            }
            data[smpl + ord] -= (predict >> coef_rshift);
        }
    } else if (num_units == 2) {
        /* 2ユニット並列で合成 */
        int32_t predict[2];
        int32_t *poutput[2];
        const int32_t *pcoef[2];
        for (u = 0; u < num_units; u += 2) {
            poutput[0] = &data[(u + 0) * nsmpls_per_unit];
            poutput[1] = &data[(u + 1) * nsmpls_per_unit];
            pcoef[0] = &coef[(u + 0) * nparams_per_unit];
            pcoef[1] = &coef[(u + 1) * nparams_per_unit];
            for (smpl = 0; smpl < nsmpls_per_unit - nparams_per_unit; smpl++) {
                predict[0] = predict[1] = half;
                for (ord = 0; ord < nparams_per_unit; ord++) {
                    predict[0] += (pcoef[0][ord] * poutput[0][smpl + ord]);
                    predict[1] += (pcoef[1][ord] * poutput[1][smpl + ord]);
                }
                poutput[0][smpl + ord] -= (predict[0] >> coef_rshift);
                poutput[1][smpl + ord] -= (predict[1] >> coef_rshift);
            }
        }
    } else {
        /* 4ユニット並列で合成 */
        int32_t predict[4];
        int32_t *poutput[4];
        const int32_t *pcoef[4];
        for (u = 0; u < num_units; u += 4) {
            poutput[0] = &data[(u + 0) * nsmpls_per_unit];
            poutput[1] = &data[(u + 1) * nsmpls_per_unit];
            poutput[2] = &data[(u + 2) * nsmpls_per_unit];
            poutput[3] = &data[(u + 3) * nsmpls_per_unit];
            pcoef[0] = &coef[(u + 0) * nparams_per_unit];
            pcoef[1] = &coef[(u + 1) * nparams_per_unit];
            pcoef[2] = &coef[(u + 2) * nparams_per_unit];
            pcoef[3] = &coef[(u + 3) * nparams_per_unit];
            for (smpl = 0; smpl < nsmpls_per_unit - nparams_per_unit; smpl++) {
                predict[0] = predict[1] = predict[2] = predict[3] = half;
                for (ord = 0; ord < nparams_per_unit; ord++) {
                    predict[0] += (pcoef[0][ord] * poutput[0][smpl + ord]);
                    predict[1] += (pcoef[1][ord] * poutput[1][smpl + ord]);
                    predict[2] += (pcoef[2][ord] * poutput[2][smpl + ord]);
                    predict[3] += (pcoef[3][ord] * poutput[3][smpl + ord]);
                }
                poutput[0][smpl + ord] -= (predict[0] >> coef_rshift);
                poutput[1][smpl + ord] -= (predict[1] >> coef_rshift);
                poutput[2][smpl + ord] -= (predict[2] >> coef_rshift);
                poutput[3][smpl + ord] -= (predict[3] >> coef_rshift);
            }
        }
    }
}
