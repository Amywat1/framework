/**
 * @file    dev_ctx.c
 * @brief   设备状态快照实现
 * @author  HUWANGWEI
 * @date    2026-07-09
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
    s_ctx.safety_posture   = SAFETY_POSTURE_NOMINAL;
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

void dev_ctx_set_safety_posture(safety_posture_t posture)
{
    pthread_mutex_lock(&s_mutex);
    s_ctx.safety_posture = posture;
    pthread_mutex_unlock(&s_mutex);
}

void dev_ctx_set_alarm_projection(bool blocking_active,
                                  uint32_t top_code,
                                  const alarm_instance_t *list,
                                  unsigned count)
{
    if (count > ALARM_ACTIVE_MAX)
    {
        count = ALARM_ACTIVE_MAX;
    }

    pthread_mutex_lock(&s_mutex);
    s_ctx.blocking_active    = blocking_active;
    s_ctx.top_alarm_code     = top_code;
    s_ctx.active_alarm_count = count;
    if ((list != NULL) && (count > 0U))
    {
        memcpy(s_ctx.active_list, list, count * sizeof(s_ctx.active_list[0]));
    }
    else
    {
        memset(s_ctx.active_list, 0, sizeof(s_ctx.active_list));
    }
    pthread_mutex_unlock(&s_mutex);
}
