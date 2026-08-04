/**
 * @file    periodic_task.h
 * @brief   调度器周期任务注册辅助接口
 */

#ifndef RUNTIME_SCHEDULER_PERIODIC_TASK_H
#define RUNTIME_SCHEDULER_PERIODIC_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief 周期任务回调函数
 * @param ctx 注册周期任务时传入的上下文指针
 */
typedef void (*periodic_task_fn_t)(void *ctx);

/**
 * @brief  注册一个由调度器统一启动的周期任务
 * @param  name 任务名称，用于日志和线程登记。仅保存指针不做拷贝，
 *              必须是字符串字面量或生命周期覆盖整个运行期的静态存储，
 *              不得传入栈上缓冲区。
 * @param  period_ms 周期时间，单位为毫秒，必须大于 0
 * @param  fn 周期回调函数，不能为空
 * @param  ctx 传递给周期回调的上下文指针，可以为空
 * @param  sched_policy 线程调度策略
 * @param  prio 线程优先级
 * @param  stack_size 线程栈大小，0 表示使用系统默认值
 * @retval SW_OK 注册成功
 * @retval SW_ERR_PARAM 参数无效
 * @retval SW_ERR_OVERFLOW 周期任务或线程登记表已满
 * @note   任务在 scheduler_start_all() 调用后启动，当前生命周期模型下不提供停止语义：
 *         线程以 pthread_detach 创建且不可 join，进程退出即随之终止。
 * @note   周期以绝对截止时间推进（CLOCK_MONOTONIC），回调耗时不累加进下一拍；
 *         若单次回调耗时超过一个周期，错过的拍会被跳过而非追赶。
 */
sw_err_t periodic_task_register(const char        *name,
                                uint32_t           period_ms,
                                periodic_task_fn_t fn,
                                void              *ctx,
                                int                sched_policy,
                                int                prio,
                                size_t             stack_size);

/**
 * @brief  计算下一拍的绝对截止时间
 * @param  deadline  [in,out] 当前拍的截止时间，返回时被推进到下一拍
 * @param  period_ms 周期毫秒数，必须大于 0
 * @param  now       调用时刻的当前时间（CLOCK_MONOTONIC）
 * @return 本次跳过的拍数，0 表示未超时、未跳拍
 * @note   周期任务线程体的时间推进逻辑本体，独立导出以便直接验证时序行为，
 *         无需依赖真实 sleep。语义：先推进一个周期；若推进后仍不晚于 now，
 *         说明回调耗时超过一个周期，继续推进直到截止时间严格晚于 now，
 *         即跳过已错过的拍而不逐拍追赶。
 * @note   参数非法（deadline 为空或 period_ms 为 0）时不做任何修改并返回 0。
 */
uint32_t periodic_task_next_deadline(struct timespec *deadline, uint32_t period_ms, const struct timespec *now);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SCHEDULER_PERIODIC_TASK_H */
