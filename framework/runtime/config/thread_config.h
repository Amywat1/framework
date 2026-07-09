/**
 * @file    thread_config.h
 * @brief   线程统一配置（优先级 / 栈大小 / 周期 / 事件总线参数）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    应用层线程由 core/scheduler 统一创建；IO 读写后台线程由 drv_io 自行管理。
 *          优先级适用于 SCHED_OTHER（范围 0~0）或 SCHED_FIFO（范围 1~99）。
 *          SCHED_OTHER 使用 nice 值（-20~19），此处用 0 = 默认。
 */

#ifndef CONFIG_THREADING_THREAD_CONFIG_H
#define CONFIG_THREADING_THREAD_CONFIG_H

/* -------------------------------------------------------------------------
 * 事件总线配置
 * ------------------------------------------------------------------------- */

/** 普通优先级队列容量 */
#define EVENT_BUS_QUEUE_SIZE         64U

/** 高优先级队列容量（SAFETY 类 + ESTOP 硬件事件） */
#define EVENT_BUS_HI_QUEUE_SIZE      16U

/** 每种事件类型最多允许注册的 handler 数量 */
#define EVENT_BUS_MAX_SUBS_PER_EVT   8U

/* -------------------------------------------------------------------------
 * 线程配置
 * 调度策略：SCHED_OTHER（普通线程）/ SCHED_FIFO（实时线程）
 * ------------------------------------------------------------------------- */

/* 事件分发线程（SCHED_OTHER） */
#define THD_EVENT_DISPATCH_STACK     (16U * 1024U)
#define THD_EVENT_DISPATCH_NICE      0

/* 洗车工作线程（SCHED_OTHER）*/
#define THD_WASH_WORKER_STACK        (32U * 1024U)
#define THD_WASH_WORKER_NICE         0

/* 云端线程（SCHED_OTHER）*/
#define THD_CLOUD_STACK              (32U * 1024U)
#define THD_CLOUD_NICE               0
#define THD_CLOUD_REPORT_PERIOD_MS   500U  /* 状态上报周期 */

/* VFD 周期任务（SCHED_OTHER）*/
#define THD_VFD_TICK_STACK           (16U * 1024U)
#define THD_VFD_TICK_NICE            0

/* 传感器滤波周期任务（SCHED_OTHER）*/
#define THD_SENSOR_POLL_STACK        (16U * 1024U)
#define THD_SENSOR_POLL_NICE         0

/* 水路 worker 线程（SCHED_OTHER）*/
#define THD_WATER_POLL_STACK       (16U * 1024U)
#define THD_WATER_POLL_NICE        0
#define THD_WATER_POLL_PERIOD_MS   10U

/* EStop 快速通道（SCHED_FIFO，§7.3 阶段二）*/
#define THD_SAFETY_THREAD_STACK    (8U * 1024U)
#define THD_SAFETY_THREAD_PRIO     90
#define THD_SAFETY_THREAD_POLL_US  5000U

#endif /* CONFIG_THREADING_THREAD_CONFIG_H */
