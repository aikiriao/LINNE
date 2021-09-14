#ifndef LINNE_DECODER_H_INCLUDED
#define LINNE_DECODER_H_INCLUDED

#include "linne.h"
#include "linne_stdint.h"

/* デコーダコンフィグ */
struct LINNEDecoderConfig {
    uint32_t max_num_channels; /* 最大チャンネル数 */
    uint32_t max_num_layers; /* 最大レイヤー数 */
    uint32_t max_num_parameters_per_layer; /* レイヤーあたり最大パラメータ数 */
    uint8_t check_crc; /* CRCによるデータ破損検査を行うか？ 1:ON それ意外:OFF */
};

/* デコーダハンドル */
struct LINNEDecoder;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ヘッダデコード */
LINNEApiResult LINNEDecoder_DecodeHeader(
        const uint8_t *data, uint32_t data_size, struct LINNEHeader *header);

/* デコーダハンドルの作成に必要なワークサイズの計算 */
int32_t LINNEDecoder_CalculateWorkSize(const struct LINNEDecoderConfig *condig);

/* デコーダハンドルの作成 */
struct LINNEDecoder* LINNEDecoder_Create(const struct LINNEDecoderConfig *condig, void *work, int32_t work_size);

/* デコーダハンドルの破棄 */
void LINNEDecoder_Destroy(struct LINNEDecoder *decoder);

/* デコーダにヘッダをセット */
LINNEApiResult LINNEDecoder_SetHeader(
        struct LINNEDecoder *decoder, const struct LINNEHeader *header);

/* 単一データブロックデコード */
LINNEApiResult LINNEDecoder_DecodeBlock(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples,
        uint32_t *decode_size, uint32_t *num_decode_samples);

/* ヘッダを含めて全ブロックデコード */
LINNEApiResult LINNEDecoder_DecodeWhole(
        struct LINNEDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LINNE_DECODER_H_INCLUDED */
