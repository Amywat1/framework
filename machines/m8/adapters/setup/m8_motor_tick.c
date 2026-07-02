/**
 * @file    m8_motor_tick.c
 * @brief   M8 电机 tick 统一调度管理器实现。
 *
 * 各电机 setup 通过 m8_motor_tick_register() 注册 tick 函数，
 * m8_motor_tick_start() 启动单一后台线程按 20ms 节拍依次调用。
 */

#include "machines/m8/adapters/setup/m8_motor_tick.h"
#include "common/log.h"
#include <pthread.h>
#include <unistd.h>
#include <stddef.h>

/* -------------------- 私有状态 -------------------- */

/** 每次 tick 循环的休眠间隔（ms） */
#define MOTOR_TICK_INTERVAL_MS  20U

/** tick 线程栈大小 */
#define MOTOR_TICK_STACK_SIZE   (16U * 1024U)

static void  (*s_tick_fns[M8_MOTOR_TICK_MAX])(void);
static int     s_tick_count = 0;

/* -------------------- 公共 API -------------------- */

sw_err_t m8_motor_tick_register(void (*tick_fn)(void))
{
    if (tick_fn == NULL) {
        return SW_ERR_PARAM;
    }
    if (s_tick_count >= M8_MOTOR_TICK_MAX) {
        LOG_ERROR("m8_motor_tick_register: 已达上限 %d", M8_MOTOR_TICK_MAX);
        return SW_ERR_OVERFLOW;
    }
    s_tick_fns[s_tick_count] = tick_fn;
    s_tick_count++;
    return SW_OK;
}

static void *motor_tick_thread_fn(void *arg)
{
    (void)arg;
    for (;;) {
        for (int i = 0; i < s_tick_count; i++) {
            s_tick_fns[i]();
        }
        usleep((unsigned long)MOTOR_TICK_INTERVAL_MS * 1000UL);
    }
    return NULL;
}

sw_err_t m8_motor_tick_start(void)
{
    pthread_attr_t attr;
    pthread_t      tid;

    if (s_tick_count == 0) {
        LOG_WARN("m8_motor_tick_start: 无已注册 tick 函数");
    }

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MOTOR_TICK_STACK_SIZE);

    if (pthread_create(&tid, &attr, motor_tick_thread_fn, NULL) != 0) {
        pthread_attr_destroy(&attr);
        LOG_ERROR("m8_motor_tick_start: pthread_create 失败");
        return SW_ERR_HW;
    }

    pthread_attr_destroy(&attr);
    pthread_detach(tid);
    LOG_INFO("m8_motor_tick_start: tick 线程已启动，已注册 %d 个函数，间隔 %ums",
             s_tick_count, MOTOR_TICK_INTERVAL_MS);
    return SW_OK;
}
