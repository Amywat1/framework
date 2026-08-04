/**
 * @file    device_snapshot.c
 * @brief   统一设备遥测快照实现（单锁原子读写）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#include "domain/telemetry/device_snapshot.h"

#include "domain/telemetry/device_snapshot_internal.h"

#include <pthread.h>
#include <string.h>

static device_snapshot_t s_state;
static pthread_mutex_t   s_mutex = PTHREAD_MUTEX_INITIALIZER;

void device_snapshot_update_op(const operational_snapshot_t *op)
{
    if (op == NULL) {
        return;
    }

    pthread_mutex_lock(&s_mutex);
    s_state.op = *op;
    pthread_mutex_unlock(&s_mutex);
}

void device_snapshot_update_safety(const safety_snapshot_t *safety)
{
    if (safety == NULL) {
        return;
    }

    pthread_mutex_lock(&s_mutex);
    s_state.safety = *safety;
    pthread_mutex_unlock(&s_mutex);
}

void device_snapshot_set_wash_mode(wash_mode_t mode)
{
    pthread_mutex_lock(&s_mutex);
    s_state.wash.mode = mode;
    pthread_mutex_unlock(&s_mutex);
}

void device_snapshot_set_cloud_connected(bool connected)
{
    pthread_mutex_lock(&s_mutex);
    s_state.connectivity.cloud_connected = connected;
    pthread_mutex_unlock(&s_mutex);
}

device_snapshot_t device_snapshot_get(void)
{
    device_snapshot_t out;

    pthread_mutex_lock(&s_mutex);
    out = s_state;
    pthread_mutex_unlock(&s_mutex);
    return out;
}

/* 向后兼容包装：保留旧 API 供已有调用方使用 */
operational_snapshot_t operational_snapshot_get(void)
{
    return device_snapshot_get().op;
}

safety_snapshot_t safety_snapshot_get(void)
{
    return device_snapshot_get().safety;
}

bool safety_snapshot_is_warning_active(void)
{
    return safety_snapshot_get().blocking_active;
}

wash_snapshot_t wash_snapshot_get(void)
{
    return device_snapshot_get().wash;
}

void device_snapshot_reset_for_test(void)
{
    pthread_mutex_lock(&s_mutex);
    memset(&s_state, 0, sizeof(s_state));
    pthread_mutex_unlock(&s_mutex);
}
