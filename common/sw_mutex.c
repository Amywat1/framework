/**
 * @file    sw_mutex.c
 * @brief   实时路径互斥量初始化助手实现
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "common/sw_mutex.h"

#include <stddef.h>

bool sw_mutex_init_prio_inherit(pthread_mutex_t *mutex)
{
    pthread_mutexattr_t attr;

    if (mutex == NULL) {
        return false;
    }

    if (pthread_mutexattr_init(&attr) != 0) {
        (void)pthread_mutex_init(mutex, NULL);
        return false;
    }

    if (pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) != 0) {
        /* 平台不支持优先级继承：退化为默认互斥量，功能不受影响 */
        (void)pthread_mutex_init(mutex, NULL);
        (void)pthread_mutexattr_destroy(&attr);
        return false;
    }

    if (pthread_mutex_init(mutex, &attr) != 0) {
        (void)pthread_mutex_init(mutex, NULL);
        (void)pthread_mutexattr_destroy(&attr);
        return false;
    }

    (void)pthread_mutexattr_destroy(&attr);
    return true;
}
