#ifndef LINNE_INTERNAL_H_INCLUDED
#define LINNE_INTERNAL_H_INCLUDED

#include "linne.h"
#include "linne_stdint.h"

/* 本ライブラリのメモリアラインメント */
#define LINNE_MEMORY_ALIGNMENT 16
/* ブロック先頭の同期コード */
#define LINNE_BLOCK_SYNC_CODE 0xFFFF

/* 内部エンコードパラメータ */
/* プリエンファシスの係数シフト量 */
#define LINNE_PREEMPHASIS_COEF_SHIFT 5
/* プリエンファシスフィルタの適用回数 */
#define LINNE_NUM_PREEMPHASIS_FILTERS 2
/* ブロック（フレーム）あたりサンプル数 */
#define LINNE_BLOCK_NUM_SAMPLES 4096
/* LPC係数のビット幅 */
#define LINNE_LPC_COEFFICIENT_BITWIDTH 8
/* log2(ユニット数)のビット幅 */
#define LINNE_LOG2_NUM_UNITS_BITWIDTH 3
/* LPC係数右シフト量のビット幅 */
#define LINNE_RSHIFT_LPC_COEFFICIENT_BITWIDTH 4
/* 圧縮をやめて生データを出力するときの閾値（サンプルあたりビット数に占める比率） */
#define LINNE_ESTIMATED_CODELENGTH_THRESHOLD 0.95f

/* アサートマクロ */
#ifdef NDEBUG
/* 未使用変数警告を明示的に回避 */
#define LINNE_ASSERT(condition) ((void)(condition))
#else
#include <assert.h>
#define LINNE_ASSERT(condition) assert(condition)
#endif

/* 静的アサートマクロ */
#define LINNE_STATIC_ASSERT(expr) extern void assertion_failed(char dummy[(expr) ? 1 : -1])

/* ブロックデータタイプ */
typedef enum LINNEBlockDataTypeTag {
    LINNE_BLOCK_DATA_TYPE_COMPRESSDATA  = 0, /* 圧縮済みデータ */
    LINNE_BLOCK_DATA_TYPE_SILENT        = 1, /* 無音データ     */
    LINNE_BLOCK_DATA_TYPE_RAWDATA       = 2, /* 生データ       */
    LINNE_BLOCK_DATA_TYPE_INVALID       = 3  /* 無効           */
} LINNEBlockDataType;

/* 内部エラー型 */
typedef enum LINNEErrorTag {
    LINNE_ERROR_OK = 0, /* OK */
    LINNE_ERROR_NG, /* 分類不能な失敗 */
    LINNE_ERROR_INVALID_ARGUMENT, /* 不正な引数 */
    LINNE_ERROR_INVALID_FORMAT, /* 不正なフォーマット       */
    LINNE_ERROR_INSUFFICIENT_BUFFER, /* バッファサイズが足りない */
    LINNE_ERROR_INSUFFICIENT_DATA /* データサイズが足りない   */
} LINNEError;

/* パラメータプリセット */
struct LINNEParameterPreset {
    uint32_t num_layers;
    const uint32_t *num_params_list;
};

#ifdef __cplusplus
extern "C" {
#endif

/* パラメータプリセット配列 */
extern const struct LINNEParameterPreset g_linne_parameter_preset[LINNE_NUM_PARAMETER_PRESETS];

#ifdef __cplusplus
}
#endif

#endif /* LINNE_INTERNAL_H_INCLUDED */
