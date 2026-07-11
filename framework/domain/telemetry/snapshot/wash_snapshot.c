/**
 * @file    wash_snapshot.c
 * @brief   洗车会话读模型实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/domain/telemetry/snapshot/wash_snapshot.h"
#include "framework/domain/telemetry/snapshot/wash_snapshot_internal.h"
#include <pthread.h>

static wash_snapshot_t s_snap;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

void wash_snapshot_on_session_started(wash_mode_t mode)
{
    pthread_mutex_lock(&s_mutex);
    s_snap.mode = mode;
    pthread_mutex_unlock(&s_mutex);
}

wash_snapshot_t wash_snapshot_get(void)
{
    wash_snapshot_t out;

    pthread_mutex_lock(&s_mutex);
    out = s_snap;
    pthread_mutex_unlock(&s_mutex);
    return out;
}
