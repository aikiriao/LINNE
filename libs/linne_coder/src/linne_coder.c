#include "linne_coder.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

#include "linne_internal.h"
#include "linne_utility.h"

/* メモリアラインメント */
#define LINNECODER_MEMORY_ALIGNMENT 16
#define LINNECODER_LOG2_MAX_NUM_PARTITIONS 8
#define LINNECODER_MAX_NUM_PARTITIONS (1 << LINNECODER_LOG2_MAX_NUM_PARTITIONS)
#define LINNECODER_RICE_PARAMETER_BITS 5
#define LINNECODER_GAMMA_BITS(uint) (((uint) == 0) ? 1 : ((2 * LINNEUTILITY_LOG2CEIL(uint + 2)) - 1))

/* 符号化ハンドル */
struct LINNECoder {
    uint8_t alloced_by_own;
    void *work;
};

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

/* ガンマ符号の出力 */
static void Gamma_PutCode(struct BitStream *stream, uint32_t val)
{
    uint32_t ndigit;

    LINNE_ASSERT(stream != NULL);

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

    LINNE_ASSERT(stream != NULL);

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

/* 再帰的Rice符号の出力 */
static void RecursiveRice_PutCode(struct BitStream *stream, uint32_t k1, uint32_t k2, uint32_t uval)
{
    const uint32_t k1pow = 1U << k1;
    const uint32_t k2mask = (1U << k2) - 1;

    LINNE_ASSERT(stream != NULL);

    if (uval < k1pow) {
        /* 1段目で符号化 */
        BitWriter_PutBits(stream, 1, 1);
        BitWriter_PutBits(stream, uval, k1);
    } else {
        /* 1段目のパラメータで引き、2段目のパラメータでRice符号化 */
        uval -= k1pow;
        BitWriter_PutZeroRun(stream, 1 + (uval >> k2));
        BitWriter_PutBits(stream, uval & k2mask, k2);
    }
}

/* 再帰的Rice符号の取得 */
static uint32_t RecursiveRice_GetCode(struct BitStream *stream, uint32_t k1, uint32_t k2)
{
    uint32_t quot, uval;
    const uint32_t k1pow = 1U << k1;

    LINNE_ASSERT(stream != NULL);

    /* 商（alpha符号）の取得 */
    BitReader_GetZeroRunLength(stream, &quot);

    /* 商で場合分け */
    if (quot == 0) {
        BitReader_GetBits(stream, &uval, k1);
    } else {
        BitReader_GetBits(stream, &uval, k2);
        uval += k1pow + ((quot - 1) << k2);
    }

    return uval;
}

/* 最適な符号化パラメータの計算 */
static void LINNECoder_CalculateOptimalRecursiveRiceParameter(
    const double mean, uint32_t *optk1, uint32_t *optk2, double *bits_per_sample)
{
    uint32_t k1, k2;
    double rho, fk1, fk2, bps;
#define OPTX 0.5127629514437670454896078808815218508243560791015625 /* (x - 1)^2 + ln(2) x ln(x) = 0 の解 */

    /* 幾何分布のパラメータを最尤推定 */
    rho = 1.0f / (1.0f + mean);

    /* 最適なパラメータの計算 */
    k2 = (uint32_t)LINNEUTILITY_MAX(0, floor(LINNEUtility_Log2(log(OPTX) / log(1.0f - rho))));
    k1 = k2 + 1;

    /* 平均符号長の計算 */
    fk1 = pow(1.0f - rho, (double)(1 << k1));
    fk2 = pow(1.0f - rho, (double)(1 << k2));
    bps = (1.0f + k1) * (1.0f - fk1) + (1.0f + k2 + 1.0f / (1.0f - fk2)) * fk1;

    /* 結果出力 */
    (*optk2) = k2;
    (*optk1) = k2 + 1;

    if (bits_per_sample != NULL) {
        (*bits_per_sample) = bps;
    }

#undef OPTX
}

/* 符号付き整数配列の符号化 */
static void LINNECoder_EncodePartitionedRecursiveRice(struct BitStream *stream, const int32_t *data, uint32_t num_samples)
{
    uint32_t max_porder, max_num_partitions;
    uint32_t porder, part, best_porder;
    double part_mean[LINNECODER_LOG2_MAX_NUM_PARTITIONS + 1][LINNECODER_MAX_NUM_PARTITIONS];

    /* 最大分割数の決定 */
    max_porder = 1;
    while ((num_samples % (1 << max_porder)) == 0) {
        max_porder++;
    }
    max_porder = LINNEUTILITY_MIN(max_porder - 1, LINNECODER_LOG2_MAX_NUM_PARTITIONS);
    max_num_partitions = (1 << max_porder);

    /* 各分割での平均を計算 */
    {
        uint32_t smpl;
        int32_t i;

        /* 最大分割時の平均値 */
        for (part = 0; part < max_num_partitions; part++) {
            const uint32_t nsmpl = num_samples / max_num_partitions;
            double part_sum = 0.0f;
            for (smpl = 0; smpl < nsmpl; smpl++) {
                part_sum += LINNEUTILITY_SINT32_TO_UINT32(data[part * nsmpl + smpl]);
            }
            part_mean[max_porder][part] = part_sum / nsmpl;
        }

        /* より大きい分割の平均は、小さい分割の平均をマージして計算 */
        for (i = (int32_t)(max_porder - 1); i >= 0; i--) {
            for (part = 0; part < (1 << i); part++) {
                part_mean[i][part] = (part_mean[i + 1][2 * part] + part_mean[i + 1][2 * part + 1]) / 2.0f;
            }
        }
    }

    /* 各分割での符号長を計算し、最適な分割を探索 */
    {
        double min_bits = FLT_MAX;
        best_porder = 0;
        for (porder = 0; porder <= max_porder; porder++) {
            const uint32_t nsmpl = (num_samples >> porder);
            uint32_t k1, k2, prevk2;
            double bps;
            double bits = 0.0f;
            for (part = 0; part < (1 << porder); part++) {
                LINNECoder_CalculateOptimalRecursiveRiceParameter(part_mean[porder][part], &k1, &k2, &bps);
                bits += bps * nsmpl;
                if (part == 0) {
                    bits += LINNECODER_RICE_PARAMETER_BITS;
                }
                else {
                    const int32_t diff = (int32_t)k2 - (int32_t)prevk2;
                    const uint32_t udiff = LINNEUTILITY_SINT32_TO_UINT32(diff);
                    bits += LINNECODER_GAMMA_BITS(udiff);
                }
                prevk2 = k2;
            }
            if (min_bits > bits) {
                min_bits = bits;
                best_porder = porder;
            }
        }
    }

    /* 最適な分割を用いて符号化 */
    {
        uint32_t smpl, k1, k2, prevk2;
        const uint32_t nsmpl = num_samples >> best_porder;

        BitWriter_PutBits(stream, best_porder, LINNECODER_LOG2_MAX_NUM_PARTITIONS);

        for (part = 0; part < (1 << best_porder); part++) {
            LINNECoder_CalculateOptimalRecursiveRiceParameter(part_mean[best_porder][part], &k1, &k2, NULL);
            if (part == 0) {
                BitWriter_PutBits(stream, k2, LINNECODER_RICE_PARAMETER_BITS);
            } else {
                const int32_t diff = (int32_t)k2 - (int32_t)prevk2;
                Gamma_PutCode(stream, LINNEUTILITY_SINT32_TO_UINT32(diff));
            }
            prevk2 = k2;
            for (smpl = 0; smpl < nsmpl; smpl++) {
                const uint32_t uval = LINNEUTILITY_SINT32_TO_UINT32(data[part * nsmpl + smpl]);
                RecursiveRice_PutCode(stream, k1, k2, uval);
            }
        }
    }
}

/* 符号付き整数配列の復号 */
static void LINNECoder_DecodePartitionedRecursiveRice(struct BitStream *stream, int32_t *data, uint32_t num_samples)
{
    uint32_t smpl, part, nsmpl, best_porder;
    uint32_t k1, k2;

    BitReader_GetBits(stream, &best_porder, LINNECODER_LOG2_MAX_NUM_PARTITIONS);

    nsmpl = num_samples >> best_porder;
    for (part = 0; part < (1 << best_porder); part++) {
        if (part == 0) {
            BitReader_GetBits(stream, &k2, LINNECODER_RICE_PARAMETER_BITS);
        } else {
            const uint32_t udiff = Gamma_GetCode(stream);
            k2 = (uint32_t)((int32_t)k2 + LINNEUTILITY_UINT32_TO_SINT32(udiff));
        }
        k1 = k2 + 1;
        for (smpl = 0; smpl < nsmpl; smpl++) {
            const uint32_t uval = RecursiveRice_GetCode(stream, k1, k2);
            data[part * nsmpl + smpl] = LINNEUTILITY_UINT32_TO_SINT32(uval);
        }
    }
}

/* 符号付き整数配列の符号化 */
void LINNECoder_Encode(struct LINNECoder *coder, struct BitStream *stream, const int32_t *data, uint32_t num_samples)
{
    LINNE_ASSERT((stream != NULL) && (data != NULL) && (coder != NULL));
    LINNE_ASSERT(num_samples != 0);

    LINNECoder_EncodePartitionedRecursiveRice(stream, data, num_samples);
}

/* 符号付き整数配列の復号 */
void LINNECoder_Decode(struct LINNECoder *coder, struct BitStream *stream, int32_t *data, uint32_t num_samples)
{
    LINNE_ASSERT((stream != NULL) && (data != NULL) && (coder != NULL));
    LINNE_ASSERT(num_samples != 0);

    LINNECoder_DecodePartitionedRecursiveRice(stream, data, num_samples);
}
