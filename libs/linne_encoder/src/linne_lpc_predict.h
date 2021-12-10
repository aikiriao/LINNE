#ifndef LINNE_LPCPREDICTOR_H_INCLUDED
#define LINNE_LPCPREDICTOR_H_INCLUDED

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LPC係数により予測/誤差出力 */
void LINNELPC_Predict(
    const int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, int32_t *residual, uint32_t coef_rshift);

#ifdef __cplusplus
}
#endif

#endif /* LINNE_LPCPREDICTOR_H_INCLUDED */
