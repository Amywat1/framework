/**
 * @file    cloud_model.h
 * @brief   项目物模型一次注册入口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef CLOUD_CLOUD_MODEL_H
#define CLOUD_CLOUD_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cloud/cloud_point.h"
#include "common/point_table/point_table.h"
#include "common/sw_error.h"

#include <stddef.h>
#include <stdint.h>

typedef sw_err_t (*cloud_property_reply_fn_t)(const char *request_json, const point_apply_result_t *result);

/**
 * @brief  项目物模型注册包（wiring 阶段一次传入）
 *
 * @note   不含上报策略：策略是 application 层 report_scheduler 的输入格式，
 *         由项目直接注册到调度器，避免 cloud/ 反向依赖 application/。
 */
typedef struct {
    const cloud_point_entry_t *entries;
    size_t                     count;
    cloud_property_reply_fn_t  property_reply;
} cloud_model_bundle_t;

/**
 * @brief  注册物模型并安装 property_port。
 *
 * @param  bundle 项目物模型注册包，entries 和 count 必须有效。
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_PARAM 参数为空或点位表为空。
 * @note   本函数不校验 getter，不初始化 watcher，也不启动运行态。
 */
sw_err_t cloud_model_register(const cloud_model_bundle_t *bundle);

/**
 * @brief  校验物模型点位表。
 *
 * @retval SW_OK 校验成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 点位表校验失败。
 * @note   本函数只执行静态校验，不调用 getter，不初始化 watcher，不写 shadow。
 */
sw_err_t cloud_model_validate(void);

/**
 * @brief  初始化物模型运行期 watcher。
 *
 * @retval SW_OK 初始化成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 watcher 初始化失败。
 * @note   本函数会为 ON_CHANGE 点位建立 shadow，应在 validate 阶段之后、运行态启动前调用。
 */
sw_err_t cloud_model_init(void);

/**
 * @brief  按点位表索引取物模型 id。
 *
 * @param  index 点位表索引（EVT_CLOUD_POINT_DIRTY 的事件 param）。
 * @return 对应的物模型 id；索引越界或物模型未注册时返回 NULL。
 * @note   供项目在注册上报调度器时作为 report_point_id_resolver_fn_t 传入，
 *         使 ON_CHANGE 事件能定位到具体点位做增量上报。
 */
const char *cloud_model_point_id_by_index(uint32_t index);

/**
 * @brief  返回已注册的物模型点位条数
 * @return 点位条数；未注册时为 0
 * @note   供启动期资产校验判断"物模型是否已注册"。此处只答"有没有"，
 *         点位内容是否合法由 cloud_model_validate() 回答。
 */
size_t cloud_model_point_count(void);

/**
 * @brief  清空物模型注册（仅供单元测试消除用例间残留）
 * @note   `cloud_model_register(NULL)` 是参数错误而非解除注册，本模块也无 init
 *         入口可复用，故按 device_snapshot 的先例单独提供复位。生产路径不应调用：
 *         运行期清空物模型会让上报与属性下行同时失效。
 */
void cloud_model_reset_for_test(void);

/**
 * @brief  构建全量属性 JSON。
 *
 * @param  buf 输出缓冲区。
 * @param  buf_size 输出缓冲区大小。
 * @retval SW_OK 构建成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 JSON 构建失败。
 */
sw_err_t cloud_model_build_properties(char *buf, size_t buf_size);

/**
 * @brief  构建增量属性 JSON。
 *
 * @param  ids 需要输出的属性 id 列表。
 * @param  count 属性 id 数量。
 * @param  buf 输出缓冲区。
 * @param  buf_size 输出缓冲区大小。
 * @retval SW_OK 构建成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 JSON 构建失败。
 */
sw_err_t cloud_model_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size);

/**
 * @brief  应用属性下发 JSON。
 *
 * @param  json_str 属性下发 JSON 字符串。
 * @param  result 应用结果输出，可为 NULL。
 * @retval SW_OK 应用成功。
 * @retval 其他 解析或写入失败。
 * @note   主要供测试或入站适配器直连使用。
 */
sw_err_t cloud_model_apply_property_set(const char *json_str, point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_CLOUD_MODEL_H */
