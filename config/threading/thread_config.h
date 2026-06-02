/**
 * @file    thread_config.h
 * @brief   线程统一配置（优先级 / 栈大小 / 周期 / 事件总线参数）
 * @author  胡望伟
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

/** 环形队列容量（必须满足最坏情况下的突发事件积压，64 项足够此系统）*/
#define EVENT_BUS_QUEUE_SIZE         64U

/** 每种事件类型最多允许注册的 handler 数量 */
#define EVENT_BUS_MAX_SUBS_PER_EVT   8U

/* -------------------------------------------------------------------------
 * 线程配置
 * 调度策略：SCHED_OTHER（普通线程）/ SCHED_FIFO（实时线程）
 * ------------------------------------------------------------------------- */

/* 事件分发线程（SCHED_OTHER） */
#define THD_EVENT_DISPATCH_STACK     (16U * 1024U)
#define THD_EVENT_DISPATCH_NICE      0

/* IO 轮询线程（SCHED_OTHER，经 scheduler 注册）*/
#define THD_IO_POLL_STACK            (16U * 1024U)
#define THD_IO_POLL_NICE             0
#define THD_IO_POLL_PERIOD_MS        30U   /* 与 drv_io 刷新周期一致 */

/* 洗车工作线程（SCHED_OTHER）*/
#define THD_WASH_WORKER_STACK        (32U * 1024U)
#define THD_WASH_WORKER_NICE         0

/* 云端线程（SCHED_OTHER）*/
#define THD_CLOUD_STACK              (32U * 1024U)
#define THD_CLOUD_NICE               0
#define THD_CLOUD_REPORT_PERIOD_MS   500U  /* 状态上报周期 */

/* 电机状态机线程（SCHED_OTHER） */
#define THD_MOTOR_TICK_STACK         (16U * 1024U)
#define THD_MOTOR_TICK_NICE          0
#define THD_MOTOR_TICK_PERIOD_MS     10U

#endif /* CONFIG_THREADING_THREAD_CONFIG_H */
