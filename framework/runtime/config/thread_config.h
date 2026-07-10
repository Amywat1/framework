/**
 * @file    thread_config.h
 * @brief   线程统一配置（优先级 / 栈大小 / 周期）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    应用层线程由 core/scheduler 统一创建；IO 读写后台线程由 drv_io 自行管理。
 *          SCHED_FIFO 线程使用 THD_*_PRIO（1~99）；SCHED_OTHER 线程 prio 固定传 0。
 */

#ifndef CONFIG_THREADING_THREAD_CONFIG_H
#define CONFIG_THREADING_THREAD_CONFIG_H

/* -------------------------------------------------------------------------
 * 线程配置
 * 调度策略：SCHED_OTHER（普通线程）/ SCHED_FIFO（实时线程）
 * ------------------------------------------------------------------------- */

/* 事件分发线程（SCHED_OTHER） */
#define THD_EVENT_DISPATCH_STACK     (16U * 1024U)

/* 洗车工作线程（SCHED_OTHER）*/
#define THD_WASH_WORKER_STACK        (32U * 1024U)

/* 云端线程（SCHED_OTHER）*/
#define THD_CLOUD_STACK              (32U * 1024U)
#define THD_CLOUD_REPORT_PERIOD_MS   500U  /* 状态上报周期 */

/* VFD 周期任务（SCHED_OTHER）*/
#define THD_VFD_TICK_STACK           (16U * 1024U)

/* 传感器滤波周期任务（SCHED_OTHER）*/
#define THD_SENSOR_POLL_STACK        (16U * 1024U)

/* 水路 worker 线程（SCHED_OTHER）*/
#define THD_WATER_POLL_STACK         (16U * 1024U)
#define THD_WATER_POLL_PERIOD_MS     10U

/* EStop 快速通道（SCHED_FIFO，§7.3 阶段二）*/
#define THD_SAFETY_THREAD_STACK      (8U * 1024U)
#define THD_SAFETY_THREAD_PRIO       90
#define THD_SAFETY_THREAD_POLL_US    5000U

#endif /* CONFIG_THREADING_THREAD_CONFIG_H */
