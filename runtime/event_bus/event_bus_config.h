/**
 * @file    event_bus_config.h
 * @brief   事件总线容量配置（编译期固定，零动态内存）
 * @author  HUWANGWEI
 * @date    2026-07-10
 *
 * @note    调整队列深度或订阅槽上限后须重新编译 event_bus.c。
 *          与 dispatch 线程栈配置（THD_EVENT_DISPATCH_STACK）无编译耦合，
 *          集成调优时可分别修改本文件与 thread_config.h。
 */

#ifndef RUNTIME_EVENT_BUS_EVENT_BUS_CONFIG_H
#define RUNTIME_EVENT_BUS_EVENT_BUS_CONFIG_H

/* -------------------------------------------------------------------------
 * 容量依据（实测，见 tests/runtime/test_event_bus_capacity.c）
 *
 * 测量方法：按已接入项目最密的 10ms 周期档，3 个发布者各连续发布 20 拍。
 *
 * | 项目           | 实测值 | 容量 | 余量  |
 * |----------------|--------|------|-------|
 * | 普通队列稳态峰值 | 9      | 64   | 86%   |
 * | 高优先级稳态峰值 | 0      | 16   | 100%  |
 * | 单事件订阅者     | 4      | 8    | 50%   |
 *
 * 稳态水位只有容量的 14%，余量主要留给慢 handler 造成的积压：dispatch 单线程
 * 串行执行 handler，一个慢 handler 会让其后所有事件排队。实测 handler 阻塞
 * 期间队列能完整填到 64 才开始拒绝，解除阻塞后可排空至 0。
 *
 * 高优先级队列实测在普通队列填满时仍能完整接受 16 个安全事件，双队列隔离有效。
 *
 * 调整容量时请同步更新上表与该测试的断言门限（稳态峰值断言为容量的一半）。
 * ------------------------------------------------------------------------- */

/** 普通优先级队列容量（实测稳态峰值 9） */
#define EVENT_BUS_QUEUE_SIZE 64U

/** 高优先级队列容量（SAFETY 类 + ESTOP 硬件事件；实测稳态峰值 0） */
#define EVENT_BUS_HI_QUEUE_SIZE 16U

/** 每种事件类型最多允许注册的 handler 数量（实测最多 4） */
#define EVENT_BUS_MAX_SUBS_PER_EVT 8U

#endif /* RUNTIME_EVENT_BUS_EVENT_BUS_CONFIG_H */
