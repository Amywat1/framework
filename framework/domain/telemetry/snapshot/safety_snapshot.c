/**
 * @file    safety_snapshot.c
 * @brief   安全/报警读模型实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/domain/telemetry/snapshot/safety_snapshot.h"
#include "framework/domain/telemetry/snapshot/safety_snapshot_internal.h"
#include <pthread.h>
#include <string.h>

static safety_snapshot_t s_snap;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

void safety_snapshot_update(const safety_snapshot_t *snap)
{
    if (snap == NULL)
    {
        return;
    }

    pthread_mutex_lock(&s_mutex);
    s_snap = *snap;
    pthread_mutex_unlock(&s_mutex);
}

safety_snapshot_t safety_snapshot_get(void)
{
    safety_snapshot_t out;

    pthread_mutex_lock(&s_mutex);
    out = s_snap;
    pthread_mutex_unlock(&s_mutex);
    return out;
}

bool safety_snapshot_is_warning_active(void)
{
    safety_snapshot_t snap = safety_snapshot_get();

    return snap.blocking_active || (snap.posture == SAFETY_POSTURE_LOCKOUT);
}
