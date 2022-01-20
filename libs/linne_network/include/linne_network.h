#ifndef LINNE_NETWORK_H_INCLUDED
#define LINNE_NETWORK_H_INCLUDED

#include <stdint.h>

/* LINNEネット */
struct LINNENetwork;

/* LINNEネットトレーナー */
struct LINNENetworkTrainer;

#ifdef __cplusplus
extern "C" {
#endif

/* LINNEネット作成に必要なワークサイズの計算 */
int32_t LINNENetwork_CalculateWorkSize(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer);

/* LINNEネット作成 */
struct LINNENetwork *LINNENetwork_Create(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer, void *work, int32_t work_size);

/* LINNEネット破棄 */
void LINNENetwork_Destroy(struct LINNENetwork *net);

/* 各層のパラメータ数を設定 */
void LINNENetwork_SetLayerStructure(
        struct LINNENetwork *net, uint32_t num_samples, uint32_t num_layers, const uint32_t *num_params_list);

/* ロス計算 同時に残差を計算にdataに書き込む */
double LINNENetwork_CalculateLoss(
        struct LINNENetwork *net, double *data, uint32_t num_samples);

/* 最適なユニット数とパラメータの設定 */
void LINNENetwork_SetUnitsAndParameters(
        struct LINNENetwork *net, const double *input, uint32_t num_samples);

/* パラメータのクリア */
void LINNENetwork_ResetParameters(struct LINNENetwork *net);

/* 各レイヤーのユニット数取得 */
void LINNENetwork_GetLayerNumUnits(
        const struct LINNENetwork *net, uint32_t *num_units_buffer, uint32_t buffer_size);

/* パラメータ取得 */
void LINNENetwork_GetParameters(
        const struct LINNENetwork *net, double **params_buffer,
        uint32_t buffer_num_layers, uint32_t buffer_num_params_per_layer);

/* 入力データからサンプルあたりの推定符号長を求める */
double LINNENetwork_EstimateCodeLength(
        struct LINNENetwork *net,
        const double *data, uint32_t num_samples, uint32_t bits_per_sample);

/* LINNEネットトレーナー作成に必要なワークサイズ計算 */
int32_t LINNENetworkTrainer_CalculateWorkSize(
        uint32_t max_num_layers, uint32_t max_num_params_per_layer);

/* LINNEネットトレーナー作成 */
struct LINNENetworkTrainer *LINNENetworkTrainer_Create(
        uint32_t max_num_layers, uint32_t max_num_params_per_layer, void *work, int32_t work_size);

/* LINNEネットトレーナー破棄 */
void LINNENetworkTrainer_Destroy(struct LINNENetworkTrainer *trainer);

/* 学習 */
void LINNENetworkTrainer_Train(struct LINNENetworkTrainer *trainer,
        struct LINNENetwork *net, const double *input, uint32_t num_samples,
        uint32_t max_num_iteration, double learning_rate, double loss_epsilon);

#ifdef __cplusplus
}
#endif

#endif /* LINNE_NETWORK_H_INCLUDED */
