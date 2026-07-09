/**
 * @file    dev_ctx.c
 * @brief   设备状态快照实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/ports/outbound/cloud/link/cloud_link_port.h"
#include "framework/common/log.h"
#include <pthread.h>
#include <string.h>

static device_context_t s_ctx;
static pthread_mutex_t  s_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool read_cloud_connected(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops == NULL) || (ops->is_online == NULL))
    {
        return false;
    }
    return ops->is_online();
}

sw_err_t dev_ctx_init(void)
{
    pthread_mutex_lock(&s_mutex);
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.operational_mode = OP_MODE_INIT;
    s_ctx.service_enabled  = true;
    s_ctx.wash_mode        = WASH_MODE_STANDARD;
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

    snap.cloud_connected = read_cloud_connected();
    return snap;
}

operational_mode_t dev_ctx_get_operational_mode(void)
{
    operational_mode_t mode;

    pthread_mutex_lock(&s_mutex);
    mode = s_ctx.operational_mode;
    pthread_mutex_unlock(&s_mutex);
    return mode;
}

void dev_ctx_set_operational_mode(operational_mode_t mode)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.operational_mode = mode;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_service_enabled(bool enabled)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.service_enabled = enabled;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_estop_active(bool active)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.estop_active = active;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_wash_mode(wash_mode_t mode)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.wash_mode = mode;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_gantry_pos(int32_t pos)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.gantry_pos = pos;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_safety_state(safety_state_t state)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.safety_state = state;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_alarm_state(bool has_alarm, uint32_t alarm_code)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.has_alarm  = has_alarm;
    s_ctx.alarm_code = alarm_code;
    pthread_mutex_unlock(&s_mutex);
}
