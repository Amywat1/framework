/**
 * @file    dev_ctx.c
 * @brief   设备状态快照实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "service/dev_ctx/dev_ctx.h"
#include "common/log.h"
#include <pthread.h>
#include <string.h>

static device_context_t s_ctx;
static pthread_mutex_t  s_mutex = PTHREAD_MUTEX_INITIALIZER;

sw_err_t dev_ctx_init(void)
{
    pthread_mutex_lock(&s_mutex);
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.device_state  = DEV_STATE_INIT;
    s_ctx.safety_state  = SAFETY_STATE_OK;
    s_ctx.wash_step     = WASH_STEP_IDLE;
    s_ctx.wash_mode     = WASH_MODE_STANDARD;
    pthread_mutex_unlock(&s_mutex);
    LOG_INFO("dev_ctx: init ok");
    return SW_OK;
}

device_context_t dev_ctx_snapshot(void)
{
    device_context_t snap;
    pthread_mutex_lock(&s_mutex);
    snap = s_ctx;
    pthread_mutex_unlock(&s_mutex);
    return snap;
}

dev_state_t dev_ctx_get_device_state(void)
{
    dev_state_t state;

    pthread_mutex_lock(&s_mutex);
    state = s_ctx.device_state;
    pthread_mutex_unlock(&s_mutex);
    return state;
}

safety_state_t dev_ctx_get_safety_state(void)
{
    safety_state_t state;

    pthread_mutex_lock(&s_mutex);
    state = s_ctx.safety_state;
    pthread_mutex_unlock(&s_mutex);
    return state;
}

void dev_ctx_set_device_state(dev_state_t state)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.device_state = state;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_safety_state(safety_state_t state)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.safety_state = state;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_wash_progress(wash_step_t step, wash_mode_t mode)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.wash_step = step;
    s_ctx.wash_mode = mode;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_alarm_state(bool has_error)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.has_error_alarm = has_error;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_cloud_status(bool connected)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.cloud_connected = connected;
    pthread_mutex_unlock(&s_mutex);
}
