#ifndef LPCNET_H_INCLUDED
#define LPCNET_H_INCLUDED

#include <stdint.h>

/* LPCネット */
struct LPCNet;

/* LPCネットトレーナー */
struct LPCNetTrainer;

#ifdef __cplusplus
extern "C" {
#endif

/* LPCネット作成に必要なワークサイズの計算 */
int32_t LPCNet_CalculateWorkSize(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer);

/* LPCネット作成 */
struct LPCNet *LPCNet_Create(
        uint32_t max_num_samples, uint32_t max_num_layers, uint32_t max_num_parameters_per_layer, void *work, int32_t work_size);

/* LPCネット破棄 */
void LPCNet_Destroy(struct LPCNet *net);

/* 各層のパラメータ数を設定 */
void LPCNet_SetLayerStructure(
        struct LPCNet *net, uint32_t num_samples, uint32_t num_layers, const uint32_t *num_params_list);

/* ロス計算 同時に残差を計算にdataに書き込む */
double LPCNet_CalculateLoss(
        struct LPCNet *net, double *data, uint32_t num_samples);

/* Levinson-Durbin法に基づく最適なユニット数とパラメータの設定 */
void LPCNet_SetUnitsAndParametersByLevinsonDurbin(
        struct LPCNet *net, const double *input, uint32_t num_samples);

/* パラメータのクリア */
void LPCNet_ResetParameters(struct LPCNet *net);

/* 各レイヤーのユニット数取得 */
void LPCNet_GetLayerNumUnits(
        const struct LPCNet *net, uint32_t *num_units_buffer, uint32_t buffer_size);

/* パラメータ取得 */
void LPCNet_GetParameters(
        const struct LPCNet *net, double **params_buffer,
        uint32_t buffer_num_layers, uint32_t buffer_num_params_per_layer);

/* 入力データからサンプルあたりの推定符号長を求める */
double LPCNet_EstimateCodeLength(
        struct LPCNet *net,
        const double *data, uint32_t num_samples, uint32_t bits_per_sample);

/* LPCネットトレーナー作成に必要なワークサイズ計算 */
int32_t LPCNetTrainer_CalculateWorkSize(
        uint32_t max_num_layers, uint32_t max_num_params_per_layer);

/* LPCネットトレーナー作成 */
struct LPCNetTrainer *LPCNetTrainer_Create(
        uint32_t max_num_layers, uint32_t max_num_params_per_layer, void *work, int32_t work_size);

/* LPCネットトレーナー破棄 */
void LPCNetTrainer_Destroy(struct LPCNetTrainer *trainer);

/* 学習 */
void LPCNetTrainer_Train(struct LPCNetTrainer *trainer,
        struct LPCNet *net, const double *input, uint32_t num_samples,
        uint32_t max_num_iteration, double learning_rate, double loss_epsilon);

#ifdef __cplusplus
}
#endif

#endif /* LPCNET_H_INCLUDED */
