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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief 周期任务回调函数
 * @param ctx 注册周期任务时传入的上下文指针
 */
typedef void (*periodic_task_fn_t)(void *ctx);

/**
 * @brief 单个周期任务的运行观测
 *
 * 由任务自己的线程更新，经 periodic_task_get_stats() 读取。
 * 用于判断回调是否挤占周期、唤醒是否滞后，而不是作为调度正确性的依据。
 * 回调耗时是 CLOCK_MONOTONIC 墙钟，不是线程 CPU 时间——要回答的是
 * 「这一拍有没有耽误下一拍」。
 */
typedef struct {
    const char *name;              /**< 注册名，只保存指针 */
    uint32_t    period_ms;         /**< 注册周期 */
    uint32_t    run_count;         /**< 已执行的回调次数 */
    uint32_t    skip_count;        /**< 累计跳过的拍数 */
    uint32_t    skip_max;          /**< 单次推进跳过的最大拍数 */
    uint32_t    last_cb_us;        /**< 最近一次回调墙钟耗时（微秒） */
    uint32_t    max_cb_us;         /**< 回调墙钟耗时最大值（微秒） */
    uint32_t    last_wake_late_us; /**< 最近一次相对截止时间的唤醒滞后（微秒） */
    uint32_t    max_wake_late_us;  /**< 唤醒滞后最大值（微秒） */
    uint32_t    last_late_us;      /**< 最近一次相对下一拍截止的越过量（微秒） */
    uint32_t    max_late_us;       /**< 越过量最大值（微秒） */
} periodic_task_stats_t;

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
 *         若墙钟越过下一拍截止时间，错过的拍会被跳过而非追赶。
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
 *         说明本拍墙钟已越过下一拍截止（回调耗时、唤醒滞后或两者），
 *         继续推进直到截止时间严格晚于 now，即跳过已错过的拍而不逐拍追赶。
 * @note   参数非法（deadline 为空或 period_ms 为 0）时不做任何修改并返回 0。
 */
uint32_t periodic_task_next_deadline(struct timespec *deadline, uint32_t period_ms, const struct timespec *now);

/**
 * @brief  相对下一拍截止时间的越过量
 * @param  old_deadline 推进前的截止时间
 * @param  now          本拍回调结束时刻（CLOCK_MONOTONIC）
 * @param  period_ms    周期毫秒数，必须大于 0
 * @return now 比 old_deadline + period 晚了多少微秒；未越过或参数非法时返回 0
 * @note   与 periodic_task_next_deadline() 配套：越过量是跳拍判据的连续量，
 *         等于「上一拍截止到 now 的间隔」减去一个周期。恰在边界上越过量为 0，
 *         但仍可能 skipped=1（截止比较取「不晚于即跳」）。
 */
uint32_t periodic_task_late_us(const struct timespec *old_deadline, const struct timespec *now, uint32_t period_ms);

/**
 * @brief  本次跳拍是否应打 WARN
 * @param  skipped   本拍跳过的拍数
 * @param  cb_us     本拍回调墙钟耗时（微秒）
 * @param  period_ms 周期毫秒数
 * @return true 表示回调挤占了周期（cb >= period）或单次跳过不少于 2 拍；
 *         skipped 为 0 或仅因唤醒抖动跳 1 拍时返回 false
 * @note   线程体用此函数决定是否打 WARN。普通 SCHED_OTHER 任务偶发晚醒一拍
 *         是预期抖动，不作为故障输出；慢性统计走 periodic_task_get_stats()。
 */
bool periodic_task_skip_should_warn(uint32_t skipped, uint32_t cb_us, uint32_t period_ms);

/**
 * @brief  将本拍观测写入统计
 * @param  stats        待更新的统计结构；为空则忽略
 * @param  skipped      本拍跳过的拍数，来自 periodic_task_next_deadline()
 * @param  cb_us        本拍回调墙钟耗时（微秒）
 * @param  wake_late_us 相对截止时间的唤醒滞后（微秒）；首拍尚未睡眠时传 0
 * @param  late_us      相对下一拍截止的越过量（微秒），来自 periodic_task_late_us()
 * @note   周期任务线程体的计数逻辑本体，独立导出以便直接验证累加规则，
 *         无需启动真实线程。run_count 每次加一；skip_count 累加 skipped；
 *         skip_max / max_cb_us / max_wake_late_us / max_late_us 取历史最大。
 */
void periodic_task_note_cycle(periodic_task_stats_t *stats,
                              uint32_t              skipped,
                              uint32_t              cb_us,
                              uint32_t              wake_late_us,
                              uint32_t              late_us);

/**
 * @brief  已登记的周期任务数量
 */
unsigned periodic_task_count(void);

/**
 * @brief  读取指定周期任务的运行观测
 * @param  index 已登记任务的下标，范围 [0, periodic_task_count())
 * @param  out   输出统计，不能为空
 * @retval SW_OK 读取成功
 * @retval SW_ERR_PARAM out 为空
 * @retval SW_ERR_NOT_FOUND index 超出已登记范围
 */
sw_err_t periodic_task_get_stats(unsigned index, periodic_task_stats_t *out);

/**
 * @brief  清空周期任务表（仅供单元测试消除用例间残留）
 * @note   生产路径不得调用：已启动的周期线程无法回收，清空登记表
 *         不会停止它们，只会造成登记与实际线程不一致。
 */
void periodic_task_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SCHEDULER_PERIODIC_TASK_H */
