/**
 * @file    sw_mutex.h
 * @brief   实时路径互斥量初始化助手（优先级继承）
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    适用范围：凡是「可能被实时线程（SCHED_FIFO）取得、同时又被
 *          SCHED_OTHER 线程竞争」的互斥量，都应改用本助手初始化。
 *          裸 PTHREAD_MUTEX_INITIALIZER 在这种场景下构成无界优先级反转：
 *          普通优先级线程持锁期间被抢占，实时线程会阻塞在互斥量上，且阻塞
 *          时长不受任何优先级保护。
 *
 * @note    为何不提供「带静态初始化的包装类型」：
 *          静态初始化出来的互斥量已经可用，再对它调用 pthread_mutex_init
 *          是 POSIX 未定义行为，因此无法「先静态初始化、事后升级为优先级
 *          继承」。调用方必须在首次上锁前用 pthread_once 调用本函数。
 */

#ifndef COMMON_SW_MUTEX_H
#define COMMON_SW_MUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdbool.h>

/**
 * @brief  以优先级继承协议初始化互斥量
 *
 * @param  mutex  待初始化的互斥量；不得为 NULL，且必须尚未被初始化
 * @retval true   已启用 PTHREAD_PRIO_INHERIT
 * @retval false  平台不支持或属性设置失败，已退化为默认互斥量
 *
 * @note   返回 false 时互斥量依然可用，功能不受影响，只是失去优先级继承
 *         保护。调用方通常无需据此改变行为，返回值主要用于自检与测试。
 * @note   必须恰好调用一次。对已初始化的互斥量重复调用是未定义行为，
 *         允许被重复初始化的模块须用 pthread_once 包裹。
 */
bool sw_mutex_init_prio_inherit(pthread_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_SW_MUTEX_H */
