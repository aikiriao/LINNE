#ifndef LPC_H_INCLUDED
#define LPC_H_INCLUDED

#include <stdint.h>

/* LPC係数計算ハンドル */
struct LPCCalculator;

/* API結果型 */
typedef enum LPCApiResultTag {
    LPC_APIRESULT_OK,                     /* OK */
    LPC_APIRESULT_NG,                     /* 分類不能なエラー */
    LPC_APIRESULT_INVALID_ARGUMENT,       /* 不正な引数 */
    LPC_APIRESULT_EXCEED_MAX_ORDER,       /* 最大次数を超えた */
    LPC_APIRESULT_FAILED_TO_CALCULATION   /* 計算に失敗 */
} LPCApiResult;

#ifdef __cplusplus
extern "C" {
#endif

/* LPC係数計算ハンドルのワークサイズ計算 */
int32_t LPCCalculator_CalculateWorkSize(uint32_t max_order);

/* LPC係数計算ハンドルの作成 */
struct LPCCalculator *LPCCalculator_Create(uint32_t max_order, void *work, int32_t work_size);

/* LPC係数計算ハンドルの破棄 */
void LPCCalculator_Destroy(struct LPCCalculator *lpcc);

/* Levinson-Durbin再帰計算によりLPC係数を求める */
LPCApiResult LPCCalculator_CalculateLPCCoefficients(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order);

/* 補助関数法よりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsAF(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    uint32_t max_num_iteration);

/* 共分散によるBurg法によりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsCovBurg(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order);

/* 入力データからサンプルあたりの推定符号長を求める */
LPCApiResult LPCCalculator_EstimateCodeLength(
        struct LPCCalculator *lpcc,
        const double *data, uint32_t num_samples, uint32_t bits_per_sample,
        uint32_t coef_order, double *length_per_sample_bits);

/* LPC係数の整数量子化 */
LPCApiResult LPC_QuantizeCoefficients(
    const double *double_coef, uint32_t coef_order, uint32_t nbits_precision,
    int32_t *int_coef, uint32_t *coef_rshift);

/* LPC係数により予測/誤差出力 */
LPCApiResult LPC_Predict(
    const int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, int32_t *residual, uint32_t coef_rshift);

/* LPC係数により合成(in-place) */
LPCApiResult LPC_Synthesize(
    int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift);

#ifdef __cplusplus
}
#endif

#endif /* LPC_H_INCLUDED */
