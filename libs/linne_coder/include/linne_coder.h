#ifndef LINNECODER_H_INCLUDED
#define LINNECODER_H_INCLUDED

#include <stdint.h>
#include "bit_stream.h"

/* 符号化ハンドル */
struct LINNECoder;

#ifdef __cplusplus
extern "C" {
#endif

/* 符号化ハンドルの作成に必要なワークサイズの計算 */
int32_t LINNECoder_CalculateWorkSize(void);

/* 符号化ハンドルの作成 */
struct LINNECoder* LINNECoder_Create(void *work, int32_t work_size);

/* 符号化ハンドルの破棄 */
void LINNECoder_Destroy(struct LINNECoder *coder);

/* 符号付き整数配列の符号化 */
void LINNECoder_Encode(struct LINNECoder *coder, struct BitStream *stream, const int32_t *data, uint32_t num_samples);

/* 符号付き整数配列の復号 */
void LINNECoder_Decode(struct LINNECoder *coder, struct BitStream *stream, int32_t *data, uint32_t num_samples);

#ifdef __cplusplus
}
#endif

#endif /* LINNECODER_H_INCLUDED */
