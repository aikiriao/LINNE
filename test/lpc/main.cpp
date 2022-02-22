#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/lpc/src/lpc.c"
}

/* ハンドル作成破棄テスト */
TEST(LPCCalculatorTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct LPCCalculatorConfig config;

        /* 最低限構造体本体よりは大きいはず */
        config.max_order = 1;
        config.max_num_samples = 1;
        work_size = LPCCalculator_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > sizeof(struct LPCCalculator));

        /* 不正なコンフィグ */
        EXPECT_TRUE(LPCCalculator_CalculateWorkSize(0) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct LPCCalculatorConfig config;
        struct LPCCalculator *lpcc;

        config.max_order = 1;
        config.max_num_samples = 1;
        work_size = LPCCalculator_CalculateWorkSize(&config);
        work = malloc(work_size);

        lpcc = LPCCalculator_Create(&config, work, work_size);
        ASSERT_TRUE(lpcc != NULL);
        EXPECT_TRUE(lpcc->work == work);
        EXPECT_EQ(lpcc->alloced_by_own, 0);

        LPCCalculator_Destroy(lpcc);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct LPCCalculator *lpcc;
        struct LPCCalculatorConfig config;

        config.max_order = 1;
        config.max_num_samples = 1;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        ASSERT_TRUE(lpcc != NULL);
        EXPECT_TRUE(lpcc->work != NULL);
        EXPECT_EQ(lpcc->alloced_by_own, 1);

        LPCCalculator_Destroy(lpcc);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct LPCCalculator *lpcc;
        struct LPCCalculatorConfig config;

        config.max_order = 1;
        config.max_num_samples = 1;
        work_size = LPCCalculator_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        lpcc = LPCCalculator_Create(NULL,    work, work_size);
        EXPECT_TRUE(lpcc == NULL);
        lpcc = LPCCalculator_Create(&config, NULL, work_size);
        EXPECT_TRUE(lpcc == NULL);
        lpcc = LPCCalculator_Create(&config, work, 0);
        EXPECT_TRUE(lpcc == NULL);

        /* コンフィグパラメータが不正 */
        config.max_order = 0; config.max_num_samples = 1;
        lpcc = LPCCalculator_Create(&config, work, work_size);
        EXPECT_TRUE(lpcc == NULL);
        config.max_order = 1; config.max_num_samples = 0;
        lpcc = LPCCalculator_Create(&config, work, work_size);
        EXPECT_TRUE(lpcc == NULL);

        free(work);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct LPCCalculator *lpcc;
        struct LPCCalculatorConfig config;

        /* コンフィグパラメータが不正 */
        config.max_order = 0; config.max_num_samples = 1;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        EXPECT_TRUE(lpcc == NULL);
        config.max_order = 1; config.max_num_samples = 0;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        EXPECT_TRUE(lpcc == NULL);
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
