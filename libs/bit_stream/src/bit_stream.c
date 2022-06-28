#include "bit_stream.h"
#include <stdint.h>

/* 下位ビットを取り出すマスク 32bitまで */
const uint32_t g_bitstream_lower_bits_mask[33] = {
    0x00000000UL,
    0x00000001UL, 0x00000003UL, 0x00000007UL, 0x0000000FUL,
    0x0000001FUL, 0x0000003FUL, 0x0000007FUL, 0x000000FFUL,
    0x000001FFUL, 0x000003FFUL, 0x000007FFUL, 0x00000FFFUL,
    0x00001FFFUL, 0x00003FFFUL, 0x00007FFFUL, 0x0000FFFFUL,
    0x0001FFFFUL, 0x0003FFFFUL, 0x0007FFFFUL, 0x000FFFFFUL,
    0x001FFFFFUL, 0x000FFFFFUL, 0x007FFFFFUL, 0x00FFFFFFUL,
    0x01FFFFFFUL, 0x03FFFFFFUL, 0x07FFFFFFUL, 0x0FFFFFFFUL,
    0x1FFFFFFFUL, 0x3FFFFFFFUL, 0x7FFFFFFFUL, 0xFFFFFFFFUL
};

/* 0のラン長パターンテーブル（注意：上位ビットからのラン長） */
const uint32_t g_bitstream_zerobit_runlength_table[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#if !defined(BITSTREAM_USE_MACROS)

/* ビットリーダのオープン */
void BitReader_Open(struct BitStream *stream, const uint8_t *memory, size_t size)
{
    /* 引数チェック */
    assert(stream != NULL);
    assert(memory != NULL);
    
    /* 内部状態リセット */
    stream->flags = 0;
    
    /* バッファ初期化 */
    stream->bit_count   = 0;
    stream->bit_buffer  = 0;
    
    /* メモリセット */
    stream->memory_image = memory;
    stream->memory_size = size;
    stream->memory_tail = memory + size;
    
    /* 読み出し位置は先頭に */
    stream->memory_p = (uint8_t *)(memory);
    
    /* 読みモードとしてセット */
    stream->flags |= (uint8_t)BITSTREAM_FLAGS_MODE_READ;
}

/* ビットライタのオープン */
void BitWriter_Open(struct BitStream *stream, const uint8_t *memory, size_t size)
{
    /* 引数チェック */
    assert(stream != NULL);
    assert(memory != NULL);
    
    /* 内部状態リセット */
    stream->flags = 0;
    
    /* バッファ初期化 */
    stream->bit_count = 8;
    stream->bit_buffer = 0;
    
    /* メモリセット */
    stream->memory_image = memory;
    stream->memory_size = size;
    stream->memory_tail = memory + size;
    
    /* 読み出し位置は先頭に */
    stream->memory_p = (uint8_t *)(memory);
    
    /* 書きモードとしてセット */
    stream->flags &= (uint8_t)(~BITSTREAM_FLAGS_MODE_READ);
}

/* ビットストリームのクローズ */
void BitStream_Close(struct BitStream *stream)
{
    /* 引数チェック */
    assert(stream != NULL);
    
    /* 残ったデータをフラッシュ */
    BitStream_Flush(stream);

    /* バッファのクリア */
    stream->bit_buffer = 0;

    /* メモリ情報のクリア */
    stream->memory_image = NULL;
    stream->memory_size  = 0;

    /* 内部状態のクリア */
    stream->memory_p     = NULL;
    stream->flags        = 0;
}

/* シーク(fseek準拠) */
void BitStream_Seek(struct BitStream *stream, int32_t offset, int32_t origin)
{
    uint8_t *pos = NULL;
    
    /* 引数チェック */
    assert(stream != NULL);
    
    /* 内部バッファをクリア（副作用が起こる） */
    BitStream_Flush(stream);
    
    /* 起点をまず定める */
    switch (origin) {
    case BITSTREAM_SEEK_CUR:
        pos = stream->memory_p;
        break;
    case BITSTREAM_SEEK_SET:
        pos = (uint8_t *)stream->memory_image;
        break;
    case BITSTREAM_SEEK_END:
        pos = (uint8_t *)((stream)->memory_tail - 1);
        break;
    default:
        assert(0);
    }
    
    /* オフセット分動かす */
    pos += (offset);
    
    /* 範囲チェック */
    assert(pos >= stream->memory_image);
    assert(pos < (stream)->memory_tail);
    
    /* 結果の保存 */
    stream->memory_p = pos;
}

/* 現在位置(ftell)準拠 */
void BitStream_Tell(struct BitStream *stream, int32_t *result)
{
    /* 引数チェック */
    assert(stream != NULL);
    assert(result != NULL);
    
    /* アクセスオフセットを返す */
    (*result) = (int32_t)(stream->memory_p - stream->memory_image);
}

/* valの右側（下位）nbits 出力（最大32bit出力可能） */
void BitWriter_PutBits(struct BitStream *stream, uint32_t val, uint32_t nbits)
{
    /* 引数チェック */
    assert(stream != NULL);

    /* 読み込みモードでは実行不可能 */
    assert(!(stream->flags & BITSTREAM_FLAGS_MODE_READ));

    /* 出力可能な最大ビット数を越えている */
    assert(nbits <= (sizeof(uint32_t) * 8));

    /* 0ビット出力は何もせず終了 */
    if (nbits == 0) { return; }

    /* valの上位ビットから順次出力
    * 初回ループでは端数（出力に必要なビット数）分を埋め出力
    * 2回目以降は8bit単位で出力 */
    while (nbits >= stream->bit_count) {
        nbits -= stream->bit_count;
        stream->bit_buffer |= BITSTREAM_GETLOWERBITS(val >> nbits, stream->bit_count);

        /* 終端に達していないかチェック */
        assert(stream->memory_p >= stream->memory_image);
        assert(stream->memory_p < stream->memory_tail);

        /* メモリに書き出し */
        (*stream->memory_p) = (stream->bit_buffer & 0xFF);
        stream->memory_p++;

        /* バッファをリセット */
        stream->bit_buffer = 0;
        stream->bit_count = 8;
    }

    /* 端数ビットの処理: 残った分をバッファの上位ビットにセット */
    assert(nbits <= 8);
    stream->bit_count -= nbits;
    stream->bit_buffer |= BITSTREAM_GETLOWERBITS(val, nbits) << stream->bit_count;
}

/* 0のランに続いて終わりの1を出力 */
void BitWriter_PutZeroRun(struct BitStream *stream, uint32_t runlength)
{
    uint32_t run = runlength + 1;

    /* 引数チェック */
    assert(stream != NULL);

    /* 読み込みモードでは実行不可能 */
    assert(!(stream->flags & BITSTREAM_FLAGS_MODE_READ));

    /* 31ビット単位で出力 */
    while (run > 31) {
        BitWriter_PutBits(stream, 0, 31);
        run -= 31;
    }

    /* 終端の1を出力 */
    BitWriter_PutBits(stream, 1, run);
}

/* nbits 取得（最大32bit）し、その値を右詰めして出力 */
void BitReader_GetBits(struct BitStream *stream, uint32_t *val, uint32_t nbits)
{
    uint8_t  ch;
    uint32_t tmp = 0;
    
    /* 引数チェック */
    assert(stream != NULL);
    assert(val != NULL);
    
    /* 読み込みモードでない場合はアサート */
    assert(stream->flags & BITSTREAM_FLAGS_MODE_READ);
    
    /* 入力可能な最大ビット数を越えている */
    assert(nbits <= (sizeof(uint32_t) * 8));
    
    /* 最上位ビットからデータを埋めていく
    * 初回ループではtmpの上位ビットにセット
    * 2回目以降は8bit単位で入力しtmpにセット */
    while (nbits > stream->bit_count) {
        nbits -= stream->bit_count;
        tmp |= BITSTREAM_GETLOWERBITS(stream->bit_buffer, stream->bit_count) << nbits;
        
        /* 終端に達していないかチェック */
        assert(stream->memory_p >= stream->memory_image);
        assert(stream->memory_p < stream->memory_tail);
        
        /* メモリから読み出し */
        ch = (*stream->memory_p);
        stream->memory_p++;
        
        stream->bit_buffer = ch;
        stream->bit_count = 8;
    }

    /* 端数ビットの処理残ったビット分をtmpの最上位ビットにセット */
    stream->bit_count -= nbits;
    tmp |= BITSTREAM_GETLOWERBITS(stream->bit_buffer >> stream->bit_count, nbits);

    /* 正常終了 */
    (*val) = tmp;
}

/* つぎの1にぶつかるまで読み込み、その間に読み込んだ0のランレングスを取得 */
void BitReader_GetZeroRunLength(struct BitStream *stream, uint32_t *runlength)
{
    uint32_t run;
    
    /* 引数チェック */
    assert(stream != NULL);
    assert(runlength != NULL);
    
    /* 上位ビットからの連続する0を計測 */
    run = g_bitstream_zerobit_runlength_table[BITSTREAM_GETLOWERBITS(stream->bit_buffer, stream->bit_count)];
    run += stream->bit_count - 8;
    
    /* 読み込んだ分カウントを減らす */
    stream->bit_count -= run;
    
    /* バッファが空の時 */
    while (stream->bit_count == 0) {
        /* 1バイト読み込みとエラー処理 */
        uint8_t ch;
        uint32_t tmp_run;
        
        /* 終端に達していないかチェック */
        assert(stream->memory_p >= stream->memory_image);
        assert(stream->memory_p < stream->memory_tail);
        
        /* メモリから読み出し */
        ch = (*stream->memory_p);
        stream->memory_p++;
        
        /* ビットバッファにセットし直して再度ランを計測 */
        stream->bit_buffer = ch;
        /* テーブルによりラン長を取得 */
        tmp_run = g_bitstream_zerobit_runlength_table[stream->bit_buffer];
        stream->bit_count = 8 - tmp_run;
        /* ランを加算 */
        run += tmp_run;
    }
    
    /* 続く1を空読み */
    stream->bit_count -= 1;
    
    /* 正常終了 */
    (*(runlength)) = run;
}

/* バッファにたまったビットをクリア */
void BitStream_Flush(struct BitStream *stream)
{
    /* 引数チェック */
    assert(stream != NULL);

    /* 既に先頭にあるときは何もしない */
    if (stream->bit_count < 8) {
        /* 読み込み位置を次のバイト先頭に */
        if (stream->flags & BITSTREAM_FLAGS_MODE_READ) {
            /* 残りビット分を空読み */
            uint32_t dummy;
            BitReader_GetBits(stream, &dummy, stream->bit_count);
        } else {
            /* バッファに余ったビットを強制出力 */
            BitWriter_PutBits(stream, 0, stream->bit_count);
        }
    }
}

#endif /* BITSTREAM_USE_MACROS */
