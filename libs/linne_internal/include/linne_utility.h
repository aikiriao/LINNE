#ifndef LINNEUTILITY_H_INCLUDED
#define LINNEUTILITY_H_INCLUDED

#include "linne_stdint.h"

/* 未使用引数警告回避 */
#define LINNEUTILITY_UNUSED_ARGUMENT(arg)  ((void)(arg))
/* 算術右シフト */
#if ((((int32_t)-1) >> 1) == ((int32_t)-1))
/* 算術右シフトが有効な環境では、そのまま右シフト */
#define LINNEUTILITY_SHIFT_RIGHT_ARITHMETIC(sint32, rshift) ((sint32) >> (rshift))
#else
/* 算術右シフトが無効な環境では、自分で定義する ハッカーのたのしみのより引用 */
/* 注意）有効範囲:0 <= rshift <= 32 */
#define LINNEUTILITY_SHIFT_RIGHT_ARITHMETIC(sint32, rshift) ((((uint64_t)(sint32) + 0x80000000UL) >> (rshift)) - (0x80000000UL >> (rshift)))
#endif
/* 符号関数 ハッカーのたのしみより引用 補足）val==0の時は0を返す */
#define LINNEUTILITY_SIGN(val)  (((val) > 0) - ((val) < 0))
/* nの倍数への切り上げ */
#define LINNEUTILITY_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))
/* 最大値の取得 */
#define LINNEUTILITY_MAX(a,b) (((a) > (b)) ? (a) : (b))
/* 最小値の取得 */
#define LINNEUTILITY_MIN(a,b) (((a) < (b)) ? (a) : (b))
/* 最小値以上最小値以下に制限 */
#define LINNEUTILITY_INNER_VALUE(val, min, max) (LINNEUTILITY_MIN((max), LINNEUTILITY_MAX((min), (val))))
/* 2の冪乗か？ */
#define LINNEUTILITY_IS_POWERED_OF_2(val) (!((val) & ((val) - 1)))
/* 符号付き32bit数値を符号なし32bit数値に一意変換 */
#define LINNEUTILITY_SINT32_TO_UINT32(sint) (((int32_t)(sint) < 0) ? ((uint32_t)((-((sint) << 1)) - 1)) : ((uint32_t)(((sint) << 1))))
/* 符号なし32bit数値を符号付き32bit数値に一意変換 */
#define LINNEUTILITY_UINT32_TO_SINT32(uint) ((int32_t)((uint) >> 1) ^ -(int32_t)((uint) & 1))
/* 絶対値の取得 */
#define LINNEUTILITY_ABS(val)               (((val) > 0) ? (val) : -(val))

/* NLZ（最上位ビットから1に当たるまでのビット数）の計算 */
#if defined(__GNUC__)
/* ビルトイン関数を使用 */
#define LINNEUTILITY_NLZ(x) (((x) > 0) ? (uint32_t)__builtin_clz(x) : 32U)
#elif defined(_MSC_VER)
/* ビルトイン関数を使用 */
__inline uint32_t LINNEUTILITY_NLZ(uint32_t x)
{
    unsigned long result;
    return (_BitScanReverse(&result, x) != 0) ? (31U - result) : 32U;
}
#else
/* ソフトウェア実装を使用 */
#define LINNEUTILITY_NLZ(x) LINNEUtility_NLZSoft(x)
#endif

/* ceil(log2(val))の計算 */
#define LINNEUTILITY_LOG2CEIL(x) (32U - LINNEUTILITY_NLZ((uint32_t)((x) - 1U)))
/* floor(log2(val))の計算 */
#define LINNEUTILITY_LOG2FLOOR(x) (31U - LINNEUTILITY_NLZ(x))

/* 2の冪乗数(1,2,4,8,16,...)への切り上げ */
#if defined(__GNUC__) || defined(_MSC_VER)
/* ビルトイン関数を使用 */
#define LINNEUTILITY_ROUNDUP2POWERED(x) (1U << LINNEUTILITY_LOG2CEIL(x))
#else
/* ソフトウェア実装を使用 */
#define LINNEUTILITY_ROUNDUP2POWERED(x) LINNEUtility_RoundUp2PoweredSoft(x)
#endif

/* 2次元配列の領域ワークサイズ計算 */
#define LINNE_CALCULATE_2DIMARRAY_WORKSIZE(type, size1, size2)\
    ((size1) * ((int32_t)sizeof(type *) + LINNE_MEMORY_ALIGNMENT\
        + (size2) * (int32_t)sizeof(type) + LINNE_MEMORY_ALIGNMENT))

/* 3次元配列の領域ワークサイズ計算 */
#define LINNE_CALCULATE_3DIMARRAY_WORKSIZE(type, size1, size2, size3)\
    ((size1) * ((int32_t)sizeof(type **) + LINNE_MEMORY_ALIGNMENT\
        + (size2) * ((int32_t)sizeof(type *) + LINNE_MEMORY_ALIGNMENT\
            + (size3) * (int32_t)sizeof(type) + LINNE_MEMORY_ALIGNMENT)))

/* 2次元配列の領域割当て */
#define LINNE_ALLOCATE_2DIMARRAY(ptr, work_ptr, type, size1, size2)\
    do {\
        uint32_t i;\
        (work_ptr) = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);\
        (ptr) = (type **)work_ptr;\
        (work_ptr) += sizeof(type *) * (size1);\
        for (i = 0; i < (size1); i++) {\
            (work_ptr) = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);\
            (ptr)[i] = (type *)work_ptr;\
            (work_ptr) += sizeof(type) * (size2);\
        }\
    } while (0)

/* 3次元配列の領域割当て */
#define LINNE_ALLOCATE_3DIMARRAY(ptr, work_ptr, type, size1, size2, size3)\
    do {\
        uint32_t i, j;\
        (work_ptr) = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);\
        (ptr) = (type ***)work_ptr;\
        (work_ptr) += sizeof(type **) * (size1);\
        for (i = 0; i < (size1); i++) {\
            (work_ptr) = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);\
            (ptr)[i] = (type **)work_ptr;\
            (work_ptr) += sizeof(type *) * (size2);\
            for (j = 0; j < (size2); j++) {\
                (work_ptr) = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNE_MEMORY_ALIGNMENT);\
                (ptr)[i][j] = (type *)work_ptr;\
                (work_ptr) += sizeof(type) * (size3);\
            }\
        }\
    } while (0)

/* プリエンファシス/デエンファシスフィルタ */
struct LINNEPreemphasisFilter {
    int32_t prev;
    int32_t coef;
};

#ifdef __cplusplus
extern "C" {
#endif

/* round関数(C89で用意されていない) */
double LINNEUtility_Round(double d);

/* CRC16(IBM)の計算 */
uint16_t LINNEUtility_CalculateCRC16(const uint8_t *data, uint64_t data_size);

/* NLZ（最上位ビットから1に当たるまでのビット数）の計算 */
uint32_t LINNEUtility_NLZSoft(uint32_t val);

/* 2の冪乗に切り上げる */
uint32_t LINNEUtility_RoundUp2PoweredSoft(uint32_t val);

/* LR -> MS (in-place) */
void LINNEUtility_MSConversion(int32_t **buffer, uint32_t num_samples);

/* MS -> LR (in-place) */
void LINNEUtility_LRConversion(int32_t **buffer, uint32_t num_samples);

/* プリエンファシスフィルタ初期化 */
void LINNEPreemphasisFilter_Initialize(struct LINNEPreemphasisFilter *preem);

/* プリエンファシスフィルタ係数計算 */
void LINNEPreemphasisFilter_CalculateCoefficient(
        struct LINNEPreemphasisFilter *preem, const int32_t *buffer, uint32_t num_samples);

/* プリエンファシス */
void LINNEPreemphasisFilter_Preemphasis(
        struct LINNEPreemphasisFilter *preem, int32_t *buffer, uint32_t num_samples);

/* デエンファシス */
void LINNEPreemphasisFilter_Deemphasis(
        struct LINNEPreemphasisFilter *preem, int32_t *buffer, uint32_t num_samples);

#ifdef __cplusplus
}
#endif

#endif /* LINNEUTILITY_H_INCLUDED */
