#ifndef LINNEPLAYER_H_INCLUDED
#define LINNEPLAYER_H_INCLUDED

#include <stdint.h>

/* 出力要求コールバック */
typedef void (*LINNESampleRequestCallback)(
        int32_t **buffer, uint32_t num_channels, uint32_t num_samples);

/* プレイヤー初期化コンフィグ */
struct LINNEPlayerConfig {
    uint32_t sampling_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    LINNESampleRequestCallback sample_request_callback;
};

#ifdef __cplusplus
extern "C" {
#endif

/* 初期化 この関数内でデバイスドライバの初期化を行い、再生開始 */
void LINNEPlayer_Initialize(const struct LINNEPlayerConfig *config);

/* 終了 初期化したときのリソースの開放はここで */
void LINNEPlayer_Finalize(void);

#ifdef __cplusplus
}
#endif

#endif /* LINNEPLAYER_H_INCLUDED */
