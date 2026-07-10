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

#ifndef EVENT_BUS_CONFIG_H
#define EVENT_BUS_CONFIG_H

/** 普通优先级队列容量 */
#define EVENT_BUS_QUEUE_SIZE       64U

/** 高优先级队列容量（SAFETY 类 + ESTOP 硬件事件） */
#define EVENT_BUS_HI_QUEUE_SIZE    16U

/** 每种事件类型最多允许注册的 handler 数量 */
#define EVENT_BUS_MAX_SUBS_PER_EVT 8U

#endif /* EVENT_BUS_CONFIG_H */
