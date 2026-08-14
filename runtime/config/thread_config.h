/**
 * @file    thread_config.h
 * @brief   线程统一配置（优先级 / 栈大小 / 周期）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    应用层线程由 runtime/scheduler 统一创建；IO 读写后台线程由 drv_io 自行管理。
 *          SCHED_FIFO 线程使用 THD_*_PRIO（1~99）；SCHED_OTHER 线程 prio 固定传 0。
 */

#ifndef RUNTIME_CONFIG_THREAD_CONFIG_H
#define RUNTIME_CONFIG_THREAD_CONFIG_H

/* -------------------------------------------------------------------------
 * SCHED_OTHER 线程（prio 固定传 0）
 * ------------------------------------------------------------------------- */
#define THD_EVENT_DISPATCH_STACK      (16U * 1024U) /**< 事件分发栈 */
#define THD_CMD_CONTROL_STACK         (16U * 1024U) /**< 命令控制栈 */
#define THD_WASH_WORKER_STACK         (32U * 1024U) /**< 洗车工作栈 */
#define THD_HOME_WORKER_STACK         (32U * 1024U) /**< 归位工作栈 */
#define THD_CLOUD_STACK               (32U * 1024U) /**< 云端栈 */
#define THD_CLOUD_REPORT_PERIOD_MS    500U          /**< 状态上报周期（ms） */
#define THD_VFD_TICK_STACK            (16U * 1024U) /**< VFD 周期任务栈 */
#define THD_SENSOR_POLL_STACK         (16U * 1024U) /**< 传感器滤波栈 */
#define THD_FLUID_PATH_POLL_STACK     (16U * 1024U) /**< 水路 worker 栈 */
#define THD_FLUID_PATH_POLL_PERIOD_MS 10U           /**< 水路轮询周期（ms） */

/* -------------------------------------------------------------------------
 * SCHED_FIFO 急停通道（§7.3 阶段二）
 * ------------------------------------------------------------------------- */
#define THD_SAFETY_THREAD_STACK   (8U * 1024U) /**< 急停线程栈 */
#define THD_SAFETY_THREAD_PRIO    90           /**< 实时优先级（1~99） */
#define THD_SAFETY_THREAD_POLL_US 5000U        /**< 轮询周期（us） */

#endif /* RUNTIME_CONFIG_THREAD_CONFIG_H */
