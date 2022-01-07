#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/linne_coder/src/linne_coder.c"
}

/* ハンドル作成破棄テスト */
TEST(LINNECoderTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;

        /* 最低限構造体本体よりは大きいはず */
        work_size = LINNECoder_CalculateWorkSize();
        ASSERT_TRUE(work_size > sizeof(struct LINNECoder));
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct LINNECoder *coder;

        work_size = LINNECoder_CalculateWorkSize();
        work = malloc(work_size);

        coder = LINNECoder_Create(work, work_size);
        ASSERT_TRUE(coder != NULL);
        EXPECT_TRUE(coder->work == work);
        EXPECT_EQ(coder->alloced_by_own, 0);

        LINNECoder_Destroy(coder);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct LINNECoder *coder;

        coder = LINNECoder_Create(NULL, 0);
        ASSERT_TRUE(coder != NULL);
        EXPECT_TRUE(coder->work != NULL);
        EXPECT_EQ(coder->alloced_by_own, 1);

        LINNECoder_Destroy(coder);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct LINNECoder *coder;

        work_size = LINNECoder_CalculateWorkSize();
        work = malloc(work_size);

        /* 引数が不正 */
        coder = LINNECoder_Create(NULL, work_size);
        EXPECT_TRUE(coder == NULL);
        coder = LINNECoder_Create(work, 0);
        EXPECT_TRUE(coder == NULL);

        /* ワークサイズ不足 */
        coder = LINNECoder_Create(work, work_size - 1);
        EXPECT_TRUE(coder == NULL);
    }
}

/* 再帰的ライス符号テスト */
TEST(LINNECoderTest, RecursiveRiceTest)
{
    /* 長めの信号を出力してみる */
    {
#define TEST_OUTPUT_LENGTH (128)
        uint32_t i, code, is_ok;
        struct BitStream strm;
        int32_t test_output_pattern[TEST_OUTPUT_LENGTH];
        int32_t decoded[TEST_OUTPUT_LENGTH];
        uint8_t data[TEST_OUTPUT_LENGTH * 2];

        /* 出力の生成 */
        for (i = 0; i < TEST_OUTPUT_LENGTH; i++) {
            test_output_pattern[i] = i;
        }

        /* 出力 */
        BitWriter_Open(&strm, data, sizeof(data));
        LINNECoder_EncodePartitionedRecursiveRice(
                &strm, test_output_pattern, TEST_OUTPUT_LENGTH);
        BitStream_Close(&strm);

        /* 取得 */
        BitReader_Open(&strm, data, sizeof(data));
        LINNECoder_DecodePartitionedRecursiveRice(
                &strm, decoded, TEST_OUTPUT_LENGTH);
        is_ok = 1;
        for (i = 0; i < TEST_OUTPUT_LENGTH; i++) {
            if (decoded[i] != test_output_pattern[i]) {
                printf("actual:%d != test:%d \n", decoded[i], test_output_pattern[i]);
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);
        BitStream_Close(&strm);
#undef TEST_OUTPUT_LENGTH
    }

    /* 長めの信号を出力してみる（乱数） */
    {
#define TEST_OUTPUT_LENGTH (128)
        uint32_t i, code, is_ok;
        struct BitStream strm;
        int32_t test_output_pattern[TEST_OUTPUT_LENGTH];
        int32_t decoded[TEST_OUTPUT_LENGTH];
        uint8_t data[TEST_OUTPUT_LENGTH * 2];

        /* 出力の生成 */
        srand(0);
        for (i = 0; i < TEST_OUTPUT_LENGTH; i++) {
            test_output_pattern[i] = rand() % 0xFF;
        }

        /* 出力 */
        BitWriter_Open(&strm, data, sizeof(data));
        LINNECoder_EncodePartitionedRecursiveRice(
                &strm, test_output_pattern, TEST_OUTPUT_LENGTH);
        BitStream_Close(&strm);

        /* 取得 */
        BitReader_Open(&strm, data, sizeof(data));
        LINNECoder_DecodePartitionedRecursiveRice(
                &strm, decoded, TEST_OUTPUT_LENGTH);
        is_ok = 1;
        for (i = 0; i < TEST_OUTPUT_LENGTH; i++) {
            if (decoded[i] != test_output_pattern[i]) {
                printf("actual:%d != test:%d \n", decoded[i], test_output_pattern[i]);
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);
        BitStream_Close(&strm);
#undef TEST_OUTPUT_LENGTH
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
