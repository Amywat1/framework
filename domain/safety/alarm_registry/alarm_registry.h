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
 * @note   清空范围：报警目录、活动报警表、会话日志、待发领域事件。
 *         因此本函数兼作测试复位入口，单元测试可在 setUp 中调用以消除
 *         用例间的状态残留，无需另设 *_reset_for_test()。
 * @note   入站端口绑定已迁至 application/bridges/alarm_binding_bridge_bind()，
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
 *         报警码须合法且全表唯一，等级与清除策略须是已定义取值，
 *         `ALARM_CLEAR_ON_MOTION` 须配非 NONE 的 `reeval_group`、其余策略须配
 *         `ALARM_REEVAL_GROUP_NONE`。这些错误在运行期都只表现为「报警行为和
 *         配置对不上」而无任何报错，因此必须挡在装载期。
 * @note   装载会清空活动表、会话日志与待发事件——目录换了之后这些都不再对应。
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

sw_err_t alarm_registry_trigger(uint32_t code);
sw_err_t alarm_registry_clear(uint32_t code);
/**
 * @brief  对指定分组的 ON_MOTION 报警做运动结束重评估
 * @param  group 重评估分组
 * @return SW_OK
 * @note   仅删除 `condition_active==false`（动作中已判定正常）的条目；
 *         条件仍成立时保持活动，运动结束本身不会清警。
 */
sw_err_t alarm_registry_reevaluate_group(motion_reeval_group_id_t group);

void alarm_registry_on_wash_session_started(void);
void alarm_registry_on_wash_session_ended(void);
/**
 * @brief 请求复位全部可人工复位告警。
 * @note  仅清除故障条件已经消失的告警；条件仍成立时保留活动状态。
 */
void alarm_registry_reset_all(void);

bool             alarm_registry_is_active(uint32_t code);
bool             alarm_registry_has_blocking_active(void);
unsigned         alarm_registry_get_session_journal(uint32_t *buf, unsigned max);
safety_posture_t alarm_registry_safety_posture(void);

/**
 * @brief  一次持锁读出完整安全投影（活动表 + 阻塞标志 + 最高码 + 安全姿态）
 * @param  list         活动告警输出缓冲；可为 NULL（只要计数与聚合值）
 * @param  list_max     list 容量；为 0 时视同 list 为 NULL
 * @param  blocking_out 输出是否存在 MAJOR 及以上告警；可为 NULL
 * @param  top_out      输出当前最高级别告警码；可为 NULL
 * @param  posture_out  输出安全姿态；可为 NULL
 * @return 实际写入 list 的条目数（list 为 NULL 时返回当前活动告警总数）
 * @note   四项数据来自同一次持锁，不会出现活动表与安全姿态取自不同时刻。
 *         需要多项时一律走本接口，不要拼接多次单项查询。
 */
unsigned alarm_registry_copy_safety_view(alarm_instance_t *list,
                                         unsigned          list_max,
                                         bool             *blocking_out,
                                         uint32_t         *top_out,
                                         safety_posture_t *posture_out);

/**
 * @brief  取出待发域事件，并一并交出自上次取出以来的丢弃条数
 * @param  buf         事件输出缓冲
 * @param  max         buf 容量
 * @param  dropped_out 输出丢弃条数并清零；可为 NULL（此时计数继续累积）
 * @return 实际取出的事件条数
 *
 * @note   待发队列满时域事件会被丢弃：清除路径是批量操作，没有可以返回错误的
 *         调用方。丢弃条数与本批事件同锁取出，调用方拿到非 0 即意味着自己漏了
 *         若干条 TRIGGERED/CLEARED，须据此让下游重新对齐（框架内由
 *         `alarm_bridge` 发 `EVT_ALARM_RESYNC` 承担）。丢事件可以，让消费者
 *         不知道自己漏了不行。
 */
unsigned alarm_registry_pull_events(alarm_domain_event_t *buf, unsigned max, uint32_t *dropped_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_ALARM_REGISTRY_ALARM_REGISTRY_H */
