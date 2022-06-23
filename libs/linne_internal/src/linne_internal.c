#include "linne_internal.h"

/* 配列の要素数を取得 */
#define LINNE_NUM_ARRAY_ELEMENTS(array) ((sizeof(array)) / (sizeof(array[0])))
/* プリセットの要素定義 */
#define LINNE_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(array) LINNE_NUM_ARRAY_ELEMENTS(array), array
/* プリセットの定義 */
#define LINNE_DEFINE_PARAMEETR_PRESET(layer_structure, regular_terms) \
    {\
        LINNE_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(layer_structure),\
        LINNE_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(regular_terms),\
    }

/* レイヤーパラメータ配列 */
static const uint32_t layer_structure_preset1[] = {       8,  32 };
static const uint32_t layer_structure_preset2[] = {  4,  64,   8 };
static const uint32_t layer_structure_preset3[] = {  4, 128,  16 };

/* 正則化パラメータ候補配列 */
static const double regular_terms_list1[] = {                                         0.0 };
static const double regular_terms_list2[] = {                            0.0, 1.0 / 512.0 };
static const double regular_terms_list3[] = { 0.0, 1.0 / 2048.0, 1.0 / 512.0, 1.0 / 128.0 };

/* パラメータプリセット配列 */
const struct LINNEParameterPreset g_linne_parameter_preset[LINNE_NUM_PARAMETER_PRESETS] = {
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset1, regular_terms_list1),
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset1, regular_terms_list2),
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset2, regular_terms_list1),
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset2, regular_terms_list2),
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset2, regular_terms_list3),
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset3, regular_terms_list1),
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset3, regular_terms_list2),
    LINNE_DEFINE_PARAMEETR_PRESET(layer_structure_preset3, regular_terms_list3),
};
