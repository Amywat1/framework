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
#define THD_SENSOR_POLL_STACK         (16U * 1024U) /**< 其它 OTHER 周期任务栈 */
#define THD_TELEMETRY_STACK           (16U * 1024U) /**< 遥测投影重建栈 */
#define THD_FLUID_PATH_POLL_PERIOD_MS 100U          /**< 水路轮询周期（ms） */
#define THD_MOTOR_TICK_PERIOD_MS      10U           /**< 电机执行器 tick 周期（ms），亦为控制环节拍 */
#define THD_LOG_DRAIN_STACK           (16U * 1024U) /**< 日志异步排出栈 */

/* -------------------------------------------------------------------------
 * SCHED_FIFO 急停与控制环（控制环必须低于急停）
 * ------------------------------------------------------------------------- */
#define THD_SAFETY_THREAD_STACK   (8U * 1024U)  /**< 急停线程栈 */
#define THD_SAFETY_THREAD_PRIO    90            /**< 实时优先级（1~99） */
#define THD_SAFETY_THREAD_POLL_US 5000U         /**< 轮询周期（us） */
#define THD_CONTROL_LOOP_STACK    (32U * 1024U) /**< 控制环栈（多回调共用） */
#define THD_CONTROL_LOOP_PRIO     60            /**< 低于急停的控制环优先级 */
#define THD_CONTROL_LOOP_WD_STACK (8U * 1024U)  /**< 控制环看门狗栈 */
#define THD_CONTROL_LOOP_WD_PRIO  70            /**< 高于控制环、低于急停，忙等时也能抢上来 */
#define THD_CONTROL_LOOP_WD_TIMEOUT_MS 500U     /**< 心跳超时（ms），宽于电机缺拍 200 ms */
#define THD_CONTROL_LOOP_WD_POLL_MS    50U      /**< 看门狗轮询周期（ms） */
#define THD_CONTROL_LOOP_WD_STARTUP_MS 2000U    /**< 尚无心跳时的启动宽限（ms） */

#if (THD_CONTROL_LOOP_WD_PRIO) <= (THD_CONTROL_LOOP_PRIO)
#error THD_CONTROL_LOOP_WD_PRIO 必须高于控制环，才能抢占忙等
#endif
#if (THD_CONTROL_LOOP_WD_PRIO) >= (THD_SAFETY_THREAD_PRIO)
#error THD_CONTROL_LOOP_WD_PRIO 必须低于急停
#endif

#endif                                         /* RUNTIME_CONFIG_THREAD_CONFIG_H */
