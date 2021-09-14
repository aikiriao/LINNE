#include "linne_coder.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "linne_utility.h"

/* メモリアラインメント */
#define LINNECODER_MEMORY_ALIGNMENT 16
/* 再帰的ライス符号の商部分の閾値 これ以上の大きさの商はガンマ符号化 */
#define LINNECODER_QUOTPART_THRESHOULD 16
/* 固定パラメータ符号を使うか否かの閾値 */
#define LINNECODER_LOW_THRESHOULD_PARAMETER 6
/* 固定小数の小数部ビット数 */
#define LINNECODER_NUM_FRACTION_PART_BITS 8
/* 固定小数の0.5 */
#define LINNECODER_FIXED_FLOAT_0_5 (1UL << ((LINNECODER_NUM_FRACTION_PART_BITS) - 1))
/* 符号なし整数を固定小数に変換 */
#define LINNECODER_UINT32_TO_FIXED_FLOAT(u32) ((u32) << (LINNECODER_NUM_FRACTION_PART_BITS))
/* 固定小数を符号なし整数に変換 */
#define LINNECODER_FIXED_FLOAT_TO_UINT32(fixed) (uint32_t)(((fixed) + (LINNECODER_FIXED_FLOAT_0_5)) >> (LINNECODER_NUM_FRACTION_PART_BITS))
/* ゴロム符号パラメータ直接設定 */
#define LINNECODER_PARAMETER_SET(param_array, order, val) ((param_array)[(order)]) = LINNECODER_UINT32_TO_FIXED_FLOAT(val)
/* ゴロム符号パラメータ取得 : 1以上であることを担保 */
#define LINNECODER_PARAMETER_GET(param_array, order) (LINNEUTILITY_MAX(LINNECODER_FIXED_FLOAT_TO_UINT32((param_array)[(order)]), 1UL))
/* Rice符号のパラメータ更新式: 指数平滑平均により平均値を推定 */
#define RICE_PARAMETER_UPDATE(param_array, order, code)\
    do {\
        (param_array)[(order)] = (RecursiveRiceParameter)(119 * (param_array)[(order)] + 9 * LINNECODER_UINT32_TO_FIXED_FLOAT(code) + (1UL << 6)) >> 7;\
    } while (0);
/* Rice符号のパラメータ計算 2 ** ceil(log2(E(x)/2)) = E(x)/2の2の冪乗切り上げ */
#define RICE_CALCULATE_RICE_PARAMETER(param_array, order)\
    LINNEUTILITY_ROUNDUP2POWERED(LINNEUTILITY_MAX(LINNECODER_FIXED_FLOAT_TO_UINT32((param_array)[(order)] >> 1), 1UL))
#define RICE_CALCULATE_LOG2_RICE_PARAMETER(param_array, order)\
    LINNEUTILITY_LOG2CEIL(LINNEUTILITY_MAX(LINNECODER_FIXED_FLOAT_TO_UINT32((param_array)[(order)] >> 1), 1UL))

/* 再帰的ライス符号パラメータ型 */
typedef uint64_t RecursiveRiceParameter;

/* 符号化ハンドル */
struct LINNECoder {
    RecursiveRiceParameter rice_parameter[2];
    RecursiveRiceParameter init_rice_parameter[2];
    uint8_t alloced_by_own;
    void *work;
};

/* ゴロム符号化の出力 */
static void LINNEGolomb_PutCode(struct BitStream *stream, uint32_t m, uint32_t val)
{
    uint32_t quot;
    uint32_t rest;
    uint32_t b, two_b;

    assert(stream != NULL);
    assert(m != 0);

    /* 商部分長と剰余部分の計算 */
    quot = val / m;
    rest = val % m;

    /* 前半部分の出力(unary符号) */
    while (quot > 0) {
        BitWriter_PutBits(stream, 0, 1);
        quot--;
    }
    BitWriter_PutBits(stream, 1, 1);

    /* 剰余部分の出力 */
    if (LINNEUTILITY_IS_POWERED_OF_2(m)) {
        /* mが2の冪: ライス符号化 m == 1の時は剰余0だから何もしない */
        if (m > 1) {
            BitWriter_PutBits(stream, rest, LINNEUTILITY_LOG2CEIL(m));
        }
        return;
    }

    /* ゴロム符号化 */
    b = LINNEUTILITY_LOG2CEIL(m);
    two_b = (uint32_t)(1UL << b);
    if (rest < (two_b - m)) {
        BitWriter_PutBits(stream, rest, b - 1);
    } else {
        BitWriter_PutBits(stream, rest + two_b - m, b);
    }
}

/* ゴロム符号の取得 */
static uint32_t LINNEGolomb_GetCode(struct BitStream *stream, uint32_t m)
{
    uint32_t quot, rest, b, two_b;

    assert(stream != NULL);
    assert(m != 0);

    /* 前半のunary符号部分を読み取り */
    BitReader_GetZeroRunLength(stream, &quot);

    /* 剰余部の桁数 */
    b = LINNEUTILITY_LOG2CEIL(m);

    /* 剰余部分の読み取り */
    if (LINNEUTILITY_IS_POWERED_OF_2(m)) {
        /* mが2の冪: ライス符号化 */
        BitReader_GetBits(stream, &rest, b);
        return (uint32_t)((quot << b) + rest);
    }

    /* ゴロム符号化 */
    two_b = (uint32_t)(1UL << b);
    BitReader_GetBits(stream, &rest, b - 1);
    if (rest < (two_b - m)) {
        return (uint32_t)(quot * m + rest);
    } else {
        uint32_t buf;
        rest <<= 1;
        BitReader_GetBits(stream, &buf, 1);
        rest += buf;
        return (uint32_t)(quot * m + rest - (two_b - m));
    }
}

/* ガンマ符号の出力 */
static void Gamma_PutCode(struct BitStream *stream, uint32_t val)
{
    uint32_t ndigit;

    assert(stream != NULL);

    if (val == 0) {
        /* 符号化対象が0ならば1を出力して終了 */
        BitWriter_PutBits(stream, 1, 1);
        return;
    }

    /* 桁数を取得 */
    ndigit = LINNEUTILITY_LOG2CEIL(val + 2);
    /* 桁数-1だけ0を続ける */
    BitWriter_PutBits(stream, 0, ndigit - 1);
    /* 桁数を使用して符号語を2進数で出力 */
    BitWriter_PutBits(stream, val + 1, ndigit);
}

/* ガンマ符号の取得 */
static uint32_t Gamma_GetCode(struct BitStream *stream)
{
    uint32_t ndigit;
    uint32_t bitsbuf;

    assert(stream != NULL);

    /* 桁数を取得 */
    /* 1が出現するまで桁数を増加 */
    BitReader_GetZeroRunLength(stream, &ndigit);
    /* 最低でも1のため下駄を履かせる */
    ndigit++;

    /* 桁数が1のときは0 */
    if (ndigit == 1) {
        return 0;
    }

    /* 桁数から符号語を出力 */
    BitReader_GetBits(stream, &bitsbuf, ndigit - 1);
    return (uint32_t)((1UL << (ndigit - 1)) + bitsbuf - 1);
}

/* 商部分（アルファ符号）を出力 */
static void LINNERecursiveRice_PutQuotPart(struct BitStream *stream, uint32_t quot)
{
    assert(stream != NULL);

    if (quot == 0) {
        BitWriter_PutBits(stream, 1, 1);
        return;
    }

    assert(quot < 32);
    BitWriter_PutBits(stream, 0, quot);
    BitWriter_PutBits(stream, 1, 1);
}

/* 商部分（アルファ符号）を取得 */
static uint32_t LINNERecursiveRice_GetQuotPart(struct BitStream *stream)
{
    uint32_t quot;

    assert(stream != NULL);

    BitReader_GetZeroRunLength(stream, &quot);

    return quot;
}

/* 剰余部分を出力 kは剰余部の桁数（logを取ったライス符号） */
static void LINNERecursiveRice_PutRestPart(struct BitStream *stream, uint32_t val, uint32_t k)
{
    assert(stream != NULL);

    /* k == 0の時はスキップ（剰余は0で確定だから） */
    if (k > 0) {
        BitWriter_PutBits(stream, val, k);
    }
}

/* 剰余部分を取得 kは剰余部の桁数（logを取ったライス符号） */
static uint32_t LINNERecursiveRice_GetRestPart(struct BitStream *stream, uint32_t k)
{
    uint32_t rest;

    assert(stream != NULL);

    /* ライス符号の剰余部分取得 */
    BitReader_GetBits(stream, &rest, k);

    return rest;
}

/* 再帰的ライス符号の出力 */
static void LINNERecursiveRice_PutCode(
        struct BitStream *stream, RecursiveRiceParameter *rice_parameters, uint32_t val)
{
    uint32_t k, param, quot;

    assert(stream != NULL);
    assert(rice_parameters != NULL);
    assert(LINNECODER_PARAMETER_GET(rice_parameters, 0) != 0);

    k = RICE_CALCULATE_LOG2_RICE_PARAMETER(rice_parameters, 0);
    param = (1U << k);
    /* パラメータ更新 */
    RICE_PARAMETER_UPDATE(rice_parameters, 0, val);
    /* 現在のパラメータ値よりも小さければ、符号化を行う */
    if (val < param) {
        /* パラメータ段数は0 */
        LINNERecursiveRice_PutQuotPart(stream, 0);
        /* 剰余部分 */
        LINNERecursiveRice_PutRestPart(stream, val, k);
        /* これで終わり */
        return;
    }
    /* 現在のパラメータ値で減じる */
    val -= param;

    k = RICE_CALCULATE_LOG2_RICE_PARAMETER(rice_parameters, 1);
    quot = 1 + (val >> k);
    /* 商が大きい場合はガンマ符号を使用する */
    if (quot < LINNECODER_QUOTPART_THRESHOULD) {
        LINNERecursiveRice_PutQuotPart(stream, quot);
    } else {
        LINNERecursiveRice_PutQuotPart(stream, LINNECODER_QUOTPART_THRESHOULD);
        Gamma_PutCode(stream, quot - LINNECODER_QUOTPART_THRESHOULD);
    }
    LINNERecursiveRice_PutRestPart(stream, val, k);
    /* パラメータ更新 */
    RICE_PARAMETER_UPDATE(rice_parameters, 1, val);
}

/* 再帰的ライス符号の取得 */
static uint32_t LINNERecursiveRice_GetCode(struct BitStream *stream, RecursiveRiceParameter *rice_parameters)
{
    uint32_t quot, val, k, param;

    assert(stream != NULL);
    assert(rice_parameters != NULL);
    assert(LINNECODER_PARAMETER_GET(rice_parameters, 0) != 0);

    /* 商部分を取得 */
    quot = LINNERecursiveRice_GetQuotPart(stream);

    /* 段数が1段目で終わっていた */
    if (quot == 0) {
        /* 剰余部分を取得/パラメータ更新して終了 */
        k = RICE_CALCULATE_LOG2_RICE_PARAMETER(rice_parameters, 0);
        val = LINNERecursiveRice_GetRestPart(stream, k);
        RICE_PARAMETER_UPDATE(rice_parameters, 0, val);
        return val;
    }

    /* 商が大きかった場合 */
    if (quot == LINNECODER_QUOTPART_THRESHOULD) {
        quot += Gamma_GetCode(stream);
    }

    /* 1段目のパラメータを符号値に設定 */
    param = RICE_CALCULATE_RICE_PARAMETER(rice_parameters, 0);
    val = param;

    /* ライス符号の剰余部を使うためlog2でパラメータ取得 */
    k = RICE_CALCULATE_LOG2_RICE_PARAMETER(rice_parameters, 1);
    val += ((quot - 1) << k);
    val += LINNERecursiveRice_GetRestPart(stream, k);

    /* パラメータ更新 */
    RICE_PARAMETER_UPDATE(rice_parameters, 0, val);
    RICE_PARAMETER_UPDATE(rice_parameters, 1, val - param);

    return val;
}

/* 符号化ハンドルの作成に必要なワークサイズの計算 */
int32_t LINNECoder_CalculateWorkSize(void)
{
    int32_t work_size;

    /* ハンドル分のサイズ */
    work_size = sizeof(struct LINNECoder) + LINNECODER_MEMORY_ALIGNMENT;

    return work_size;
}

/* 符号化ハンドルの作成 */
struct LINNECoder* LINNECoder_Create(void *work, int32_t work_size)
{
    struct LINNECoder *coder;
    uint8_t tmp_alloc_by_own = 0;
    uint8_t *work_ptr;

    /* ワーク領域時前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        /* 引数を自前の計算値に差し替える */
        if ((work_size = LINNECoder_CalculateWorkSize()) < 0) {
            return NULL;
        }
        work = malloc((uint32_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((work == NULL) || (work_size < LINNECoder_CalculateWorkSize())) {
        return NULL;
    }

    /* ワーク領域先頭取得 */
    work_ptr = (uint8_t *)work;

    /* ハンドル領域確保 */
    work_ptr = (uint8_t *)LINNEUTILITY_ROUNDUP((uintptr_t)work_ptr, LINNECODER_MEMORY_ALIGNMENT);
    coder = (struct LINNECoder *)work_ptr;
    work_ptr += sizeof(struct LINNECoder);

    /* ハンドルメンバ設定 */
    coder->alloced_by_own = tmp_alloc_by_own;
    coder->work = work;

    return coder;
}

/* 符号化ハンドルの破棄 */
void LINNECoder_Destroy(struct LINNECoder *coder)
{
    if (coder != NULL) {
        /* 自前確保していたら領域開放 */
        if (coder->alloced_by_own == 1) {
            free(coder->work);
        }
    }
}

/* 初期パラメータの計算 */
static void LINNECoder_CalculateInitialRecursiveRiceParameter(
        struct LINNECoder *coder, const int32_t *data, uint32_t num_samples)
{
    uint32_t smpl, i, init_param;
    uint64_t sum;

    assert((coder != NULL) && (data != NULL));

    /* パラメータ初期値（符号平均値）の計算 */
    sum = 0;
    for (smpl = 0; smpl < num_samples; smpl++) {
        sum += LINNEUTILITY_SINT32_TO_UINT32(data[smpl]);
    }

    init_param = (uint32_t)LINNEUTILITY_MAX(sum / num_samples, 1);

    /* 初期パラメータのセット */
    for (i = 0; i < 2; i++) {
        LINNECODER_PARAMETER_SET(coder->init_rice_parameter, i, init_param);
        LINNECODER_PARAMETER_SET(coder->rice_parameter, i, init_param);
    }
}

/* 再帰的ライス符号のパラメータを符号化（量子化による副作用あり） */
static void LINNECoder_PutInitialRecursiveRiceParameter(struct LINNECoder *coder, struct BitStream *stream)
{
    uint32_t i, first_order_param, log2_param;

    assert((stream != NULL) && (coder != NULL));

    /* 1次パラメータを取得 */
    first_order_param = LINNECODER_PARAMETER_GET(coder->init_rice_parameter, 0);

    /* 記録のためlog2をとる */
    log2_param = LINNEUTILITY_LOG2CEIL(first_order_param);

    /* 2の冪乗を取って量子化 */
    first_order_param = 1U << log2_param;

    /* 書き出し */
    assert(log2_param < 32);
    BitWriter_PutBits(stream, log2_param, 5);

    /* パラメータ反映 */
    for (i = 0; i < 2; i++) {
        LINNECODER_PARAMETER_SET(coder->init_rice_parameter, i, first_order_param);
        LINNECODER_PARAMETER_SET(coder->rice_parameter, i, first_order_param);
    }
}

/* 再帰的ライス符号のパラメータを取得 */
static void LINNECoder_GetInitialRecursiveRiceParameter(struct LINNECoder *coder, struct BitStream *stream)
{
    uint32_t i, first_order_param, log2_param;

    assert((stream != NULL) && (coder != NULL));

    /* 初期パラメータの取得 */
    BitReader_GetBits(stream, &log2_param, 5);
    assert(log2_param < 32);
    first_order_param = 1U << log2_param;

    /* 初期パラメータの設定 */
    for (i = 0; i < 2; i++) {
        LINNECODER_PARAMETER_SET(coder->init_rice_parameter, i, (uint32_t)first_order_param);
        LINNECODER_PARAMETER_SET(coder->rice_parameter, i, (uint32_t)first_order_param);
    }
}

/* 符号付き整数配列の符号化 */
void LINNECoder_Encode(struct LINNECoder *coder, struct BitStream *stream, const int32_t *data, uint32_t num_samples)
{
    uint32_t smpl;

    assert((stream != NULL) && (data != NULL) && (coder != NULL));
    assert(num_samples != 0);

    /* 先頭にパラメータ初期値を書き込む */
    LINNECoder_CalculateInitialRecursiveRiceParameter(coder, data, num_samples);
    LINNECoder_PutInitialRecursiveRiceParameter(coder, stream);

    if (LINNECODER_PARAMETER_GET(coder->init_rice_parameter, 0) > LINNECODER_LOW_THRESHOULD_PARAMETER) {
        /* パラメータを適応的に変更しつつ符号化 */
        for (smpl = 0; smpl < num_samples; smpl++) {
            LINNERecursiveRice_PutCode(stream, coder->rice_parameter, LINNEUTILITY_SINT32_TO_UINT32(data[smpl]));
        }
    } else {
        /* パラメータが小さい場合はパラメータ固定で符号 */
        for (smpl = 0; smpl < num_samples; smpl++) {
            LINNEGolomb_PutCode(stream, LINNECODER_PARAMETER_GET(coder->init_rice_parameter, 0), LINNEUTILITY_SINT32_TO_UINT32(data[smpl]));
        }
    }
}

/* 符号付き整数配列の復号 */
void LINNECoder_Decode(struct LINNECoder *coder, struct BitStream *stream, int32_t *data, uint32_t num_samples)
{
    uint32_t smpl, abs;

    assert((stream != NULL) && (data != NULL) && (coder != NULL));

    /* パラメータ初期値の取得 */
    LINNECoder_GetInitialRecursiveRiceParameter(coder, stream);

    if (LINNECODER_PARAMETER_GET(coder->init_rice_parameter, 0) > LINNECODER_LOW_THRESHOULD_PARAMETER) {
        /* パラメータを適応的に変更しつつ符号化 */
        for (smpl = 0; smpl < num_samples; smpl++) {
            abs = LINNERecursiveRice_GetCode(stream, coder->rice_parameter);
            data[smpl] = LINNEUTILITY_UINT32_TO_SINT32(abs);
        }
    } else {
        /* パラメータが小さい場合はパラメータ固定でゴロム符号化 */
        for (smpl = 0; smpl < num_samples; smpl++) {
            abs = LINNEGolomb_GetCode(stream, LINNECODER_PARAMETER_GET(coder->init_rice_parameter, 0));
            data[smpl] = LINNEUTILITY_UINT32_TO_SINT32(abs);
        }
    }
}
