#ifndef LINNE_H_INCLUDED
#define LINNE_H_INCLUDED

#include "linne_stdint.h"

/* フォーマットバージョン */
#define LINNE_FORMAT_VERSION        1

/* コーデックバージョン */
#define LINNE_CODEC_VERSION         1

/* ヘッダサイズ */
#define LINNE_HEADER_SIZE           30

/* 処理可能な最大チャンネル数 */
#define LINNE_MAX_NUM_CHANNELS      8

/* パラメータプリセット数 */
#define LINNE_NUM_PARAMETER_PRESETS 3

/* API結果型 */
typedef enum LINNEApiResultTag {
    LINNE_APIRESULT_OK = 0,                  /* 成功                         */
    LINNE_APIRESULT_INVALID_ARGUMENT,        /* 無効な引数                   */
    LINNE_APIRESULT_INVALID_FORMAT,          /* 不正なフォーマット           */
    LINNE_APIRESULT_INSUFFICIENT_BUFFER,     /* バッファサイズが足りない     */
    LINNE_APIRESULT_INSUFFICIENT_DATA,       /* データが足りない             */
    LINNE_APIRESULT_PARAMETER_NOT_SET,       /* パラメータがセットされてない */
    LINNE_APIRESULT_DETECT_DATA_CORRUPTION,  /* データ破損を検知した         */
    LINNE_APIRESULT_NG                       /* 分類不能な失敗               */
} LINNEApiResult;

/* マルチチャンネル処理法 */
typedef enum LINNEChannelProcessMethodTag {
    LINNE_CH_PROCESS_METHOD_NONE = 0,  /* 何もしない     */
    LINNE_CH_PROCESS_METHOD_MS,        /* ステレオMS処理 */
    LINNE_CH_PROCESS_METHOD_INVALID    /* 無効値         */
} LINNEChannelProcessMethod;

/* ヘッダ情報 */
struct LINNEHeader {
    uint32_t format_version;                        /* フォーマットバージョン         */
    uint32_t codec_version;                         /* エンコーダバージョン           */
    uint16_t num_channels;                          /* チャンネル数                   */
    uint32_t num_samples;                           /* 1チャンネルあたり総サンプル数  */
    uint32_t sampling_rate;                         /* サンプリングレート             */
    uint16_t bits_per_sample;                       /* サンプルあたりビット数         */
    uint32_t num_samples_per_block;                 /* ブロックあたりサンプル数   */
    uint8_t preset;                                 /* パラメータプリセット         */
    LINNEChannelProcessMethod ch_process_method;    /* マルチチャンネル処理法         */
};

#endif /* LINNE_H_INCLUDED */
