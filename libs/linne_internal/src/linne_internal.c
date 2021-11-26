#include "linne_internal.h"

/* 配列の要素数を取得 */
#define LINNE_NUM_ARRAY_ELEMENTS(array) ((sizeof(array)) / (sizeof(array[0])))
/* プリセットの要素定義 */
#define LINNE_DEFINE_PARAMETER_PRESET(array) \
    {\
        LINNE_NUM_ARRAY_ELEMENTS(array),\
        array\
    }

static const uint32_t num_params_preset1[] = {       8,  32 };
static const uint32_t num_params_preset2[] = {  4,  64,  16 };
static const uint32_t num_params_preset3[] = {  4,  96,  16 };
static const uint32_t num_params_preset4[] = {  4, 128,  16 };

/* パラメータプリセット配列 */
const struct LINNEParameterPreset g_linne_parameter_preset[LINNE_NUM_PARAMETER_PRESETS] = {
    LINNE_DEFINE_PARAMETER_PRESET(num_params_preset1),
    LINNE_DEFINE_PARAMETER_PRESET(num_params_preset2),
    LINNE_DEFINE_PARAMETER_PRESET(num_params_preset3),
    LINNE_DEFINE_PARAMETER_PRESET(num_params_preset4),
};
