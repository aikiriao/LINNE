#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/linne_encoder/src/linne_encoder.c"
}

/* 有効なヘッダをセット */
#define LINNE_SetValidHeader(p_header)\
    do {\
        struct LINNEHeader *header__p       = p_header;\
        header__p->format_version           = LINNE_FORMAT_VERSION;\
        header__p->codec_version            = LINNE_CODEC_VERSION;\
        header__p->num_channels             = 1;\
        header__p->sampling_rate            = 44100;\
        header__p->bits_per_sample          = 16;\
        header__p->num_samples              = 1024;\
        header__p->num_samples_per_block    = 32;\
        header__p->preset                   = 0;\
        header__p->ch_process_method        = LINNE_CH_PROCESS_METHOD_NONE;\
    } while (0);

/* 有効なエンコードパラメータをセット */
#define LINNEEncoder_SetValidEncodeParameter(p_parameter)\
    do {\
        struct LINNEEncodeParameter *param__p = p_parameter;\
        param__p->num_channels          = 1;\
        param__p->bits_per_sample       = 16;\
        param__p->sampling_rate         = 44100;\
        param__p->num_samples_per_block = 1024;\
        param__p->preset                = 0;\
        param__p->ch_process_method     = LINNE_CH_PROCESS_METHOD_NONE;\
    } while (0);

/* 有効なコンフィグをセット */
#define LINNEEncoder_SetValidConfig(p_config)\
    do {\
        struct LINNEEncoderConfig *config__p = p_config;\
        config__p->max_num_channels             = 8;\
        config__p->max_num_samples_per_block    = 8192;\
        config__p->max_num_layers               = 4;\
        config__p->max_num_parameters_per_layer = 128;\
    } while (0);

/* ヘッダエンコードテスト */
TEST(LINNEEncoderTest, EncodeHeaderTest)
{
    /* ヘッダエンコード成功ケース */
    {
        struct LINNEHeader header;
        uint8_t data[LINNE_HEADER_SIZE] = { 0, };

        LINNE_SetValidHeader(&header);
        EXPECT_EQ(LINNE_APIRESULT_OK, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 簡易チェック */
        EXPECT_EQ('I', data[0]);
        EXPECT_EQ('B', data[1]);
        EXPECT_EQ('R', data[2]);
        EXPECT_EQ('A', data[3]);
    }

    /* ヘッダエンコード失敗ケース */
    {
        struct LINNEHeader header;
        uint8_t data[LINNE_HEADER_SIZE] = { 0, };

        /* 引数が不正 */
        LINNE_SetValidHeader(&header);
        EXPECT_EQ(LINNE_APIRESULT_INVALID_ARGUMENT, LINNEEncoder_EncodeHeader(NULL, data, sizeof(data)));
        EXPECT_EQ(LINNE_APIRESULT_INVALID_ARGUMENT, LINNEEncoder_EncodeHeader(&header, NULL, sizeof(data)));

        /* データサイズ不足 */
        LINNE_SetValidHeader(&header);
        EXPECT_EQ(LINNE_APIRESULT_INSUFFICIENT_BUFFER, LINNEEncoder_EncodeHeader(&header, data, sizeof(data) - 1));
        EXPECT_EQ(LINNE_APIRESULT_INSUFFICIENT_BUFFER, LINNEEncoder_EncodeHeader(&header, data, LINNE_HEADER_SIZE - 1));

        /* 異常なチャンネル数 */
        LINNE_SetValidHeader(&header);
        header.num_channels = 0;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なサンプル数 */
        LINNE_SetValidHeader(&header);
        header.num_samples = 0;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なサンプリングレート */
        LINNE_SetValidHeader(&header);
        header.sampling_rate = 0;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なビット深度 */
        LINNE_SetValidHeader(&header);
        header.bits_per_sample = 0;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常な最大ブロックあたりサンプル数 */
        LINNE_SetValidHeader(&header);
        header.num_samples_per_block = 0;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なプリセット */
        LINNE_SetValidHeader(&header);
        header.preset = LINNE_NUM_PARAMETER_PRESETS;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なチャンネル処理法 */
        LINNE_SetValidHeader(&header);
        header.ch_process_method = LINNE_CH_PROCESS_METHOD_INVALID;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* チャンネル処理法とチャンネル数指定の組み合わせがおかしい */
        LINNE_SetValidHeader(&header);
        header.num_channels = 1;
        header.ch_process_method = LINNE_CH_PROCESS_METHOD_MS;
        EXPECT_EQ(LINNE_APIRESULT_INVALID_FORMAT, LINNEEncoder_EncodeHeader(&header, data, sizeof(data)));
    }

}

/* エンコードハンドル作成破棄テスト */
TEST(LINNEEncoderTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct LINNEEncoderConfig config;

        /* 最低限構造体本体よりは大きいはず */
        LINNEEncoder_SetValidConfig(&config);
        work_size = LINNEEncoder_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > sizeof(struct LINNEEncoder));

        /* 不正な引数 */
        EXPECT_TRUE(LINNEEncoder_CalculateWorkSize(NULL) < 0);

        /* 不正なコンフィグ */
        LINNEEncoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        EXPECT_TRUE(LINNEEncoder_CalculateWorkSize(&config) < 0);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_samples_per_block = 0;
        EXPECT_TRUE(LINNEEncoder_CalculateWorkSize(&config) < 0);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_layers = 0;
        EXPECT_TRUE(LINNEEncoder_CalculateWorkSize(&config) < 0);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_parameters_per_layer = 0;
        EXPECT_TRUE(LINNEEncoder_CalculateWorkSize(&config) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct LINNEEncoder *encoder;
        struct LINNEEncoderConfig config;

        LINNEEncoder_SetValidConfig(&config);
        work_size = LINNEEncoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        encoder = LINNEEncoder_Create(&config, work, work_size);
        ASSERT_TRUE(encoder != NULL);
        EXPECT_TRUE(encoder->work == work);
        EXPECT_EQ(encoder->set_parameter, 0);
        EXPECT_EQ(encoder->alloced_by_own, 0);
        EXPECT_TRUE(encoder->coder != NULL);

        LINNEEncoder_Destroy(encoder);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct LINNEEncoder *encoder;
        struct LINNEEncoderConfig config;

        LINNEEncoder_SetValidConfig(&config);

        encoder = LINNEEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);
        EXPECT_TRUE(encoder->work != NULL);
        EXPECT_EQ(encoder->set_parameter, 0);
        EXPECT_EQ(encoder->alloced_by_own, 1);
        EXPECT_TRUE(encoder->coder != NULL);

        LINNEEncoder_Destroy(encoder);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct LINNEEncoder *encoder;
        struct LINNEEncoderConfig config;

        LINNEEncoder_SetValidConfig(&config);
        work_size = LINNEEncoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        encoder = LINNEEncoder_Create(NULL, work, work_size);
        EXPECT_TRUE(encoder == NULL);
        encoder = LINNEEncoder_Create(&config, NULL, work_size);
        EXPECT_TRUE(encoder == NULL);
        encoder = LINNEEncoder_Create(&config, work, 0);
        EXPECT_TRUE(encoder == NULL);

        /* ワークサイズ不足 */
        encoder = LINNEEncoder_Create(&config, work, work_size - 1);
        EXPECT_TRUE(encoder == NULL);

        /* コンフィグが不正 */
        LINNEEncoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        encoder = LINNEEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_samples_per_block = 0;
        encoder = LINNEEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_layers = 0;
        encoder = LINNEEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_parameters_per_layer = 0;
        encoder = LINNEEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        free(work);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct LINNEEncoder *encoder;
        struct LINNEEncoderConfig config;

        LINNEEncoder_SetValidConfig(&config);

        /* 引数が不正 */
        encoder = LINNEEncoder_Create(NULL, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        /* コンフィグが不正 */
        LINNEEncoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        encoder = LINNEEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_samples_per_block = 0;
        encoder = LINNEEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_layers = 0;
        encoder = LINNEEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        LINNEEncoder_SetValidConfig(&config);
        config.max_num_parameters_per_layer = 0;
        encoder = LINNEEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);
    }
}

/* 1ブロックエンコードテスト */
TEST(LINNEEncoderTest, EncodeBlockTest)
{
    /* 無効な引数 */
    {
        struct LINNEEncoder *encoder;
        struct LINNEEncoderConfig config;
        struct LINNEEncodeParameter parameter;
        int32_t *input[LINNE_MAX_NUM_CHANNELS];
        uint8_t *data;
        uint32_t ch, sufficient_size, output_size, num_samples;

        LINNEEncoder_SetValidEncodeParameter(&parameter);
        LINNEEncoder_SetValidConfig(&config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * parameter.num_channels * parameter.num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = LINNEEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* 無効な引数を渡す */
        EXPECT_EQ(
                LINNE_APIRESULT_INVALID_ARGUMENT,
                LINNEEncoder_EncodeBlock(NULL, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                LINNE_APIRESULT_INVALID_ARGUMENT,
                LINNEEncoder_EncodeBlock(encoder, NULL, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                LINNE_APIRESULT_INVALID_ARGUMENT,
                LINNEEncoder_EncodeBlock(encoder, input, 0,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                LINNE_APIRESULT_INVALID_ARGUMENT,
                LINNEEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    NULL, sufficient_size, &output_size));
        EXPECT_EQ(
                LINNE_APIRESULT_INVALID_ARGUMENT,
                LINNEEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, 0, &output_size));
        EXPECT_EQ(
                LINNE_APIRESULT_INVALID_ARGUMENT,
                LINNEEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, NULL));

        /* 領域の開放 */
        for (ch = 0; ch < parameter.num_channels; ch++) {
            free(input[ch]);
        }
        free(data);
        LINNEEncoder_Destroy(encoder);
    }

    /* パラメータ未セットでエンコード */
    {
        struct LINNEEncoder *encoder;
        struct LINNEEncoderConfig config;
        struct LINNEEncodeParameter parameter;
        int32_t *input[LINNE_MAX_NUM_CHANNELS];
        uint8_t *data;
        uint32_t ch, sufficient_size, output_size, num_samples;

        LINNEEncoder_SetValidEncodeParameter(&parameter);
        LINNEEncoder_SetValidConfig(&config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * parameter.num_channels * parameter.num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.num_samples_per_block);
            /* 無音セット */
            memset(input[ch], 0, sizeof(int32_t) * parameter.num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = LINNEEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* パラメータセット前にエンコード: エラー */
        EXPECT_EQ(
                LINNE_APIRESULT_PARAMETER_NOT_SET,
                LINNEEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));

        /* パラメータ設定 */
        EXPECT_EQ(
                LINNE_APIRESULT_OK,
                LINNEEncoder_SetEncodeParameter(encoder, &parameter));

        /* 1ブロックエンコード */
        EXPECT_EQ(
                LINNE_APIRESULT_OK,
                LINNEEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));

        /* 領域の開放 */
        for (ch = 0; ch < parameter.num_channels; ch++) {
            free(input[ch]);
        }
        free(data);
        LINNEEncoder_Destroy(encoder);
    }

    /* 無音エンコード */
    {
        struct LINNEEncoder *encoder;
        struct LINNEEncoderConfig config;
        struct LINNEEncodeParameter parameter;
        struct BitStream stream;
        int32_t *input[LINNE_MAX_NUM_CHANNELS];
        uint8_t *data;
        uint32_t ch, sufficient_size, output_size, num_samples;
        uint32_t bitbuf;

        LINNEEncoder_SetValidEncodeParameter(&parameter);
        LINNEEncoder_SetValidConfig(&config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * parameter.num_channels * parameter.num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.num_samples_per_block);
            /* 無音セット */
            memset(input[ch], 0, sizeof(int32_t) * parameter.num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = LINNEEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* パラメータ設定 */
        EXPECT_EQ(
                LINNE_APIRESULT_OK,
                LINNEEncoder_SetEncodeParameter(encoder, &parameter));

        /* 1ブロックエンコード */
        EXPECT_EQ(
                LINNE_APIRESULT_OK,
                LINNEEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));

        /* ブロック先頭の同期コードがあるので2バイトよりは大きいはず */
        EXPECT_TRUE(output_size > 2);

        /* 内容の確認 */
        BitReader_Open(&stream, data, output_size);
        /* 同期コード */
        BitReader_GetBits(&stream, &bitbuf, 16);
        EXPECT_EQ(LINNE_BLOCK_SYNC_CODE, bitbuf);
        /* ブロックデータタイプ */
        BitReader_GetBits(&stream, &bitbuf, 2);
        EXPECT_TRUE((bitbuf == LINNE_BLOCK_DATA_TYPE_COMPRESSDATA)
                || (bitbuf == LINNE_BLOCK_DATA_TYPE_SILENT)
                || (bitbuf == LINNE_BLOCK_DATA_TYPE_RAWDATA));
        /* この後データがエンコードされているので、まだ終端ではないはず */
        BitStream_Tell(&stream, (int32_t *)&bitbuf);
        EXPECT_TRUE(bitbuf < output_size);
        BitStream_Close(&stream);

        /* 領域の開放 */
        for (ch = 0; ch < parameter.num_channels; ch++) {
            free(input[ch]);
        }
        free(data);
        LINNEEncoder_Destroy(encoder);
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
