#ifndef LINNE_ENCODER_H_INCLUDED
#define LINNE_ENCODER_H_INCLUDED

#include "linne.h"
#include "linne_stdint.h"

/* エンコードパラメータ */
struct LINNEEncodeParameter {
    uint16_t num_channels; /* 入力波形のチャンネル数 */
    uint16_t bits_per_sample; /* 入力波形のサンプルあたりビット数 */
    uint32_t sampling_rate; /* 入力波形のサンプリングレート */
    uint16_t num_samples_per_block; /* ブロックあたりサンプル数 */
    uint8_t preset; /* エンコードパラメータプリセット */
    LINNEChannelProcessMethod ch_process_method;  /* マルチチャンネル処理法 */
};

/* エンコーダコンフィグ */
struct LINNEEncoderConfig {
    uint32_t max_num_channels; /* 最大チャンネル数 */
    uint32_t max_num_samples_per_block; /* 最大のブロックあたりサンプル数 */
    uint32_t max_num_layers; /* LPCNetの最大レイヤー数 */
    uint32_t max_num_parameters_per_layer; /* LPCNetのレイヤーあたり最大パラメータ数 */
};

/* エンコーダハンドル */
struct LINNEEncoder;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ヘッダエンコード */
LINNEApiResult LINNEEncoder_EncodeHeader(
    const struct LINNEHeader *header, uint8_t *data, uint32_t data_size);

/* エンコーダハンドル作成に必要なワークサイズ計算 */
int32_t LINNEEncoder_CalculateWorkSize(const struct LINNEEncoderConfig *config);

/* エンコーダハンドル作成 */
struct LINNEEncoder *LINNEEncoder_Create(const struct LINNEEncoderConfig *config, void *work, int32_t work_size);

/* エンコーダハンドルの破棄 */
void LINNEEncoder_Destroy(struct LINNEEncoder *encoder);

/* エンコードパラメータの設定 */
LINNEApiResult LINNEEncoder_SetEncodeParameter(
    struct LINNEEncoder *encoder, const struct LINNEEncodeParameter *parameter);

/* ヘッダ含めファイル全体をエンコード */
LINNEApiResult LINNEEncoder_EncodeWhole(
    struct LINNEEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LINNE_ENCODER_H_INCLUDED */
