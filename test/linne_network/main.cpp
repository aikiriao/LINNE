#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/linne_network/src/linne_network.c"
}

/* ネットワークハンドル作成破棄テスト */
TEST(LINNENetworkTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;

        /* 最低限構造体本体よりは大きいはず */
        work_size = LINNENetwork_CalculateWorkSize(1024, 10, 128);
        ASSERT_TRUE(work_size > sizeof(struct LINNENetwork));

        /* 不正な引数 */
        EXPECT_TRUE(LINNENetwork_CalculateWorkSize(   0, 10, 128) < 0);
        EXPECT_TRUE(LINNENetwork_CalculateWorkSize(1024,  0, 128) < 0);
        EXPECT_TRUE(LINNENetwork_CalculateWorkSize(1024, 10,   0) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct LINNENetwork *net;

        work_size = LINNENetwork_CalculateWorkSize(1024, 10, 128);
        work = malloc(work_size);

        net = LINNENetwork_Create(1024, 10, 128, work, work_size);
        ASSERT_TRUE(net != NULL);
        EXPECT_TRUE(net->layers != NULL);
        EXPECT_TRUE(net->layers_work != NULL);
        EXPECT_TRUE(net->lpcc != NULL);
        EXPECT_TRUE(net->data_buffer != NULL);
        EXPECT_EQ(net->max_num_samples, 1024);
        EXPECT_EQ(net->max_num_layers, 10);
        EXPECT_EQ(net->max_num_params, 128);

        LINNENetwork_Destroy(net);
        free(work);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct LINNENetwork *net;

        work_size = LINNENetwork_CalculateWorkSize(1024, 10, 128);
        work = malloc(work_size);

        /* 引数が不正 */
        net = LINNENetwork_Create(   0, 10, 128, work, work_size);
        EXPECT_TRUE(net == NULL);
        net = LINNENetwork_Create(1024,  0, 128, work, work_size);
        EXPECT_TRUE(net == NULL);
        net = LINNENetwork_Create(1024, 10,   0, work, work_size);
        EXPECT_TRUE(net == NULL);
        net = LINNENetwork_Create(1024, 10, 128, NULL, work_size);
        EXPECT_TRUE(net == NULL);
        net = LINNENetwork_Create(1024, 10, 128, work,         0);
        EXPECT_TRUE(net == NULL);

        /* ワークサイズ不足 */
        net = LINNENetwork_Create(1024, 10, 128, work, work_size - 1);
        EXPECT_TRUE(net == NULL);

        free(work);
    }

}

/* トレーナーハンドル作成破棄テスト */
TEST(LINNENetworkTrainer, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;

        /* 最低限構造体本体よりは大きいはず */
        work_size = LINNENetworkTrainer_CalculateWorkSize(10, 128);
        ASSERT_TRUE(work_size > sizeof(struct LINNENetworkTrainer));

        /* 不正な引数 */
        EXPECT_TRUE(LINNENetworkTrainer_CalculateWorkSize( 0, 128) < 0);
        EXPECT_TRUE(LINNENetworkTrainer_CalculateWorkSize(10,   0) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct LINNENetworkTrainer *trainer;

        work_size = LINNENetworkTrainer_CalculateWorkSize(10, 128);
        work = malloc(work_size);

        trainer = LINNENetworkTrainer_Create(10, 128, work, work_size);
        ASSERT_TRUE(trainer != NULL);
        EXPECT_EQ(trainer->max_num_layers, 10);
        EXPECT_EQ(trainer->max_num_params_per_layer, 128);

        LINNENetworkTrainer_Destroy(trainer);
        free(work);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct LINNENetworkTrainer *trainer;

        work_size = LINNENetworkTrainer_CalculateWorkSize(10, 128);
        work = malloc(work_size);

        /* 引数が不正 */
        trainer = LINNENetworkTrainer_Create( 0, 128, work, work_size);
        EXPECT_TRUE(trainer == NULL);
        trainer = LINNENetworkTrainer_Create(10,   0, work, work_size);
        EXPECT_TRUE(trainer == NULL);
        trainer = LINNENetworkTrainer_Create(10, 128, NULL, work_size);
        EXPECT_TRUE(trainer == NULL);
        trainer = LINNENetworkTrainer_Create(10, 128, work,         0);
        EXPECT_TRUE(trainer == NULL);

        /* ワークサイズ不足 */
        trainer = LINNENetworkTrainer_Create(10, 128, work, work_size - 1);
        EXPECT_TRUE(trainer == NULL);

        free(work);
    }

}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
