/**
 * @file    operational_snapshot.c
 * @brief   运行模式读模型实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/domain/telemetry/snapshot/operational_snapshot.h"
#include "framework/domain/telemetry/snapshot/operational_snapshot_internal.h"
#include <pthread.h>

static operational_snapshot_t s_snap;
static pthread_mutex_t        s_mutex = PTHREAD_MUTEX_INITIALIZER;

void operational_snapshot_update(const operational_snapshot_t *snap)
{
    if (snap == NULL)
    {
        return;
    }

    pthread_mutex_lock(&s_mutex);
    s_snap = *snap;
    pthread_mutex_unlock(&s_mutex);
}

operational_snapshot_t operational_snapshot_get(void)
{
    operational_snapshot_t out;

    pthread_mutex_lock(&s_mutex);
    out = s_snap;
    pthread_mutex_unlock(&s_mutex);
    return out;
}

bool operational_snapshot_is_stopping(void)
{
    operational_snapshot_t snap = operational_snapshot_get();

    if (!snap.service_enabled)
    {
        return true;
    }

    return (snap.mode == OP_MODE_INIT) ||
           (snap.mode == OP_MODE_EXCEPTION) ||
           (snap.mode == OP_MODE_RECOVERING);
}

bool operational_snapshot_is_standby(void)
{
    operational_snapshot_t snap = operational_snapshot_get();

    return (snap.mode == OP_MODE_IDLE) && snap.service_enabled;
}
