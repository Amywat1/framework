/**
 * @file    wash_catalog.h
 * @brief   套餐号到方案路径与超时的注册表
 *
 * @note    可选。行内容由项目登记。未 bind 则 find 恒为假。
 *          框架只做精确匹配，不解释套餐含义，也不改路径。
 */

#ifndef APPLICATION_WASH_CATALOG_H
#define APPLICATION_WASH_CATALOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 单机最多已发布套餐行 */
#define WASH_CATALOG_MAX 16U

/**
 * @brief  一条已发布套餐
 */
typedef struct {
    wash_mode_t mode;         /**< 套餐号；查找时精确匹配 */
    const char *program_path; /**< 方案 JSON 路径；不可空 */
    uint32_t    timeout_ms;   /**< 会话总超时；须大于 0 */
} wash_catalog_entry_t;

/**
 * @brief  登记套餐表（只存指针，表须静态或与模块同寿）
 * @param  rows  套餐行
 * @param  count 行数，1..WASH_CATALOG_MAX
 * @retval SW_OK           已登记
 * @retval SW_ERR_PARAM    表空、路径空、超时为 0、套餐号重复或为 WASH_MODE_INVALID
 * @retval SW_ERR_OVERFLOW 超过 WASH_CATALOG_MAX
 */
sw_err_t wash_catalog_bind(const wash_catalog_entry_t *rows, unsigned count);

/**
 * @brief  按套餐号精确查找已发布方案。
 * @param  mode 套餐号；不做归一
 * @param  out  命中时写入；可为 NULL 仅作存在判断
 * @retval true  已登记且有该套餐
 * @retval false 未登记或未发布
 */
bool wash_catalog_find(wash_mode_t mode, wash_catalog_entry_t *out);

/**
 * @brief  清空模块状态，仅供测试隔离
 */
void wash_catalog_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_WASH_CATALOG_H */
