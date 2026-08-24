/**
 * @file    cloud_model.h
 * @brief   项目物模型一次注册入口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef DOMAIN_CLOUD_CLOUD_MODEL_H
#define DOMAIN_CLOUD_CLOUD_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/cloud/cloud_point.h"

#include <stddef.h>

/**
 * @brief  注册并校验物模型点位表。
 *
 * @param  entries 项目静态点位表，生命周期须覆盖整个运行期。
 * @param  count   条目数。
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_PARAM 表为空、超上限或校验失败。
 * @note   校验失败不会留下半份注册。不初始化 watcher，不安装 JSON 下行。
 */
sw_err_t cloud_model_register(const cloud_point_entry_t *entries, size_t count);

/**
 * @brief  取已注册的点位表
 * @param  count_out 条目数输出，可为 NULL
 * @return 点位表首地址；未注册时返回 NULL
 * @note   供适配层构建上下行载荷。返回的是项目提供的静态表，调用方不得修改。
 */
const cloud_point_entry_t *cloud_model_entries(size_t *count_out);

/**
 * @brief  返回已注册的物模型点位条数
 * @return 点位条数；未注册时为 0
 */
size_t cloud_model_point_count(void);

/**
 * @brief  清空物模型注册（仅供单元测试消除用例间残留）
 * @note   同时复位 watcher。生产路径不应调用。
 */
void cloud_model_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_CLOUD_CLOUD_MODEL_H */
