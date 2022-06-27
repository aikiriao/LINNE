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
/* LPC係数のビット幅 */
#define LINNE_LPC_COEFFICIENT_BITWIDTH 8
/* log2(ユニット数)のビット幅 */
#define LINNE_LOG2_NUM_UNITS_BITWIDTH 3
/* LPC係数右シフト量のビット幅 */
#define LINNE_RSHIFT_LPC_COEFFICIENT_BITWIDTH 4
/* 圧縮をやめて生データを出力するときの閾値（サンプルあたりビット数に占める比率） */
#define LINNE_ESTIMATED_CODELENGTH_THRESHOLD 0.95f
/* ユニット数決定時の補助関数法の繰り返し回数（0は初期値のまま） */
#define LINNE_NUM_AF_METHOD_ITERATION_DETERMINEUNIT 0
/* 学習パラメータ */
/* 最大繰り返し回数 */
#define LINNE_TRAINING_PARAMETER_MAX_NUM_ITRATION 2000
/* 学習率 */
#define LINNE_TRAINING_PARAMETER_LEARNING_RATE 0.1f
/* ロスが変化しなくなったと判定する閾値 */
#define LINNE_TRAINING_PARAMETER_LOSS_EPSILON 1.0e-7
/* 正則化パラメータ配列サイズ */
#define LINNE_REGULARIZATION_PARAMETER_LIST_SIZE 4

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
    const uint32_t *layer_num_params_list;
    uint32_t num_regular_terms;
    const double* regular_terms_list;
    uint32_t num_coef_symbols;
    const uint32_t* coef_symbol_freq_table;
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
