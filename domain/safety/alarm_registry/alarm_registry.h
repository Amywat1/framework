/**
 * @file    alarm_registry.h
 * @brief   报警注册表聚合根
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_SAFETY_ALARM_REGISTRY_ALARM_REGISTRY_H
#define DOMAIN_SAFETY_ALARM_REGISTRY_ALARM_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/safety/model/alarm_types.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化报警注册表，并清空全部运行期状态
 *
 * @retval SW_OK 状态已清空
 *
 * @note   清空范围：报警目录、活动报警表、会话日志。不补发 CLEARED。
 *         因此本函数兼作测试复位入口，单元测试可在 setUp 中调用以消除
 *         用例间的状态残留，无需另设 *_reset_for_test()。
 * @note   入站端口绑定已迁至 application/bridges/alarm_bridge_bind()，
 *         本函数不再注册 alarm_binding。
 * @note   调用时机约束：必须在加载报警目录之前调用，否则会清掉已加载的目录。
 *         bootstrap 的 bind 阶段已保证该顺序。
 */
sw_err_t alarm_registry_init(void);

/**
 * @brief  装载报警目录，并清空全部运行期状态
 * @param  defs  目录条目数组
 * @param  count 条目数
 * @retval SW_OK          已装载
 * @retval SW_ERR_PARAM   defs 为 NULL，或目录内容非法（见下）
 * @retval SW_ERR_OVERFLOW count 超过 ALARM_CATALOG_MAX
 *
 * @note   目录内容逐条校验，任一条不合法即整表拒绝、不做部分装载：
 *         报警码须合法且全表唯一，等级与生命周期须是已定义取值，
 *         `FOLLOW_SIGNAL` 须配 `ALARM_REEVAL_GROUP_NONE`，`LATCH` 的组可空可填。
 *         这些错误在运行期都只表现为「报警行为和配置对不上」而无任何报错，
 *         因此必须挡在装载期。
 * @note   装载会清空活动表与会话日志且不补发 CLEARED——目录换了之后这些都不再对应。
 */
sw_err_t alarm_registry_load_catalog(const alarm_def_t *defs, unsigned count);

/**
 * @brief  返回已加载的报警定义条数
 * @return 目录条数；未加载或已复位时为 0
 *
 * @note   供启动期资产校验判断"报警目录是否已加载"。目录为空时框架无法
 *         把任何报警码解析为定义，trigger 会静默失败——这正是需要在启动期
 *         拦住的情形，而不是等到现场触发报警才发现报警链路不工作。
 */
unsigned alarm_registry_catalog_count(void);

/**
 * @brief  上报指定报警的故障条件成立
 * @note   新插入或驱逐腾槽后，在返回前将 `EVT_ALARM_TRIGGERED`（及姿态边沿）入队。
 *         重复 trigger 只刷新 `condition_active`，不再发 TRIGGERED。
 *         条件已成立时不取消搁置；条件从消失回到成立视为新事件，搁置下架。
 */
sw_err_t alarm_registry_trigger(uint32_t code);

/**
 * @brief  上报指定报警的故障条件消失
 * @note   跟随信号立即删除并发 `EVT_ALARM_CLEARED`；锁存只清条件、保持条目。
 */
sw_err_t alarm_registry_clear(uint32_t code);

/**
 * @brief  对指定分组的锁存报警做运动结束重评估
 * @param  group 重评估分组
 * @return SW_OK
 * @note   仅删除本组且 `condition_active==false` 的锁存条目；
 *         条件仍成立时保持活动，运动结束本身不会清警。删除在返回前发 CLEARED。
 */
sw_err_t alarm_registry_reevaluate_group(motion_reeval_group_id_t group);

void alarm_registry_on_wash_session_started(void);
void alarm_registry_on_wash_session_ended(void);

/**
 * @brief 请求复位全部可人工复位告警。
 * @note  仅清除故障条件已经消失的告警；条件仍成立时保留活动状态。
 *        删除在返回前发 CLEARED。
 */
void alarm_registry_reset_all(void);

/**
 * @brief  强制删除所有非安全触发的活动报警
 * @note   条件仍成立也删除。安全触发（码尾 09）与检测停用补偿码（形态 11）保留。
 *         删除在返回前发 CLEARED。
 */
void alarm_registry_reset_non_safety(void);

/**
 * @brief  是否存在仍成立的安全触发
 * @return 活动表中有 safety_trigger 且 condition_active 时为 true
 */
bool alarm_registry_has_safety_trigger_active(void);

bool alarm_registry_is_active(uint32_t code);
bool alarm_registry_has_blocking_active(void);

/**
 * @brief  拷贝本洗车会话的 journal 码表，并交出累计丢弃条数
 * @param  buf         码表输出缓冲；可为 NULL（只要丢弃计数）
 * @param  max         buf 容量；为 0 时不拷贝码表
 * @param  dropped_out 输出本会话因满池未记入的条数；可为 NULL
 * @return 实际写入 buf 的条数
 *
 * @note   丢弃计数随会话累积，读取不清零——journal 是多读者状态。
 *         新会话开始时码表与计数一并清零。会话结束后仍可读取，直到下一会话开始。
 *         需要与活动表同一时刻的快照时走 `alarm_registry_copy_safety_view`。
 */
unsigned         alarm_registry_get_session_journal(uint32_t *buf, unsigned max, uint32_t *dropped_out);
safety_posture_t alarm_registry_safety_posture(void);

/**
 * @brief  一次持锁读出完整安全投影（活动表 + blocking + 最高码 + 姿态 + journal）
 * @param  out 输出视图；不可为 NULL
 * @retval SW_OK        已写入
 * @retval SW_ERR_PARAM out 为 NULL
 * @note   全部字段来自同一次持锁。需要多项时一律走本接口。
 */
sw_err_t alarm_registry_copy_safety_view(alarm_safety_view_t *out);

/**
 * @brief  应用桥推送当前整机工况，供装置工况谓词使用
 * @param  mode 与 operational_mode_t 同值
 */
void alarm_registry_set_op_mode(alarm_op_mode_t mode);

/**
 * @brief  项目推送对象运行态
 * @param  layer     对象层
 * @param  object_id 对象号
 * @param  run       IDLE / COMMANDED / TRIPPED
 * @retval SW_OK        已记录并重算提示
 * @retval SW_ERR_PARAM 层或对象号非法
 */
sw_err_t alarm_registry_set_object_run(uint32_t layer, uint32_t object_id, alarm_object_run_t run);

/**
 * @brief  搁置一条已活动报警的提示
 * @param  code         报警码
 * @param  duration_ms  时限；0 非法，超过 ALARM_SHELVE_MAX_MS 按封顶
 * @retval SW_OK         已搁置
 * @retval SW_ERR_PARAM  码未知、未活动、或策略禁止
 */
sw_err_t alarm_registry_shelve(uint32_t code, uint32_t duration_ms);

/**
 * @brief  取消搁置
 */
sw_err_t alarm_registry_unshelve(uint32_t code);

/**
 * @brief  设置检测停用
 * @param  code    原码（目录 allow_oos）
 * @param  enable  true 停用，false 恢复检测
 * @retval SW_OK         已切换
 * @retval SW_ERR_PARAM  不允许停用
 */
sw_err_t alarm_registry_set_oos(uint32_t code, bool enable);

/**
 * @brief  指定原码当前是否检测停用
 */
bool alarm_registry_is_oos(uint32_t code);

/**
 * @brief  合成补偿码：同层同对象 + 故障形态 11；目录无此对象时仍可计算
 */
uint32_t alarm_registry_oos_compensating_code(uint32_t original_code);

/**
 * @brief  清除全部搁置，并把对象开停态清回 IDLE
 * @note   恢复起步调用。不清检测停用。开停态回到 IDLE，避免恢复后残留 TRIPPED/COMMANDED。
 */
void alarm_registry_clear_announce_overrides(void);

/**
 * @brief  清除全部检测停用并删除形态 11 补偿码
 * @note   上电由 init/装载目录清零；关总开关落到 STOPPED 时调用。
 *         不补发原码 TRIGGERED；检测条件仍成立时由 detector 下一拍再贴。
 */
void alarm_registry_clear_oos(void);

/**
 * @brief  到期取消搁置；由应用周期调用
 * @param  now_ms 当前单调毫秒
 */
void alarm_registry_poll(uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_ALARM_REGISTRY_ALARM_REGISTRY_H */
