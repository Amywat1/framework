/**
 * @file    m8_comm_watchdog.c
 * @brief   M8 机型周期通讯设备心跳监控实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/adapters/alarm/m8_comm_watchdog.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"
#include "framework/ports/outbound/safety/safety_cutout_port.h"
#include "framework/common/time_util.h"
#include "framework/common/log.h"
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    uint32_t timeout_ms;
    uint32_t alarm_code;
    bool     immediate_cutout;
} watchdog_cfg_t;

static const watchdog_cfg_t s_cfg[] = {
#define X(dev, timeout, cls, idx, nat, lvl, resp, clr, cut, desc) \
    { (timeout), ALARM_CODE_MAKE(cls, idx, nat), (cut) },
    M8_COMM_WATCHDOG_TABLE(X)
#undef X
};

static uint64_t        s_last_hb_ms[COMM_DEV_COUNT];
static bool            s_lost_active[COMM_DEV_COUNT];
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

sw_err_t m8_comm_watchdog_init(void)
{
    uint64_t now = time_util_get_ms();
    int      i;

    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < (int)COMM_DEV_COUNT; ++i)
    {
        s_last_hb_ms[i]  = now;
        s_lost_active[i] = false;
    }
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("m8_comm_watchdog: init ok, devices=%d", (int)COMM_DEV_COUNT);
    return SW_OK;
}

void m8_comm_watchdog_heartbeat(comm_dev_id_t dev)
{
    if ((unsigned)dev >= (unsigned)COMM_DEV_COUNT)
    {
        return;
    }

    pthread_mutex_lock(&s_mutex);
    s_last_hb_ms[(int)dev] = time_util_get_ms();
    pthread_mutex_unlock(&s_mutex);
}

void m8_comm_watchdog_poll(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    uint64_t                   now;
    int                        i;

    if (ops == NULL)
    {
        return;
    }

    now = time_util_get_ms();
    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < (int)COMM_DEV_COUNT; ++i)
    {
        bool lost = (time_elapsed_ms(s_last_hb_ms[i], now) > s_cfg[i].timeout_ms);

        if (lost)
        {
            if (!s_lost_active[i])
            {
                if (s_cfg[i].immediate_cutout)
                {
                    safety_cutout_execute();
                }
                (void)ops->trigger(s_cfg[i].alarm_code);
                s_lost_active[i] = true;
            }
        }
        else
        {
            s_lost_active[i] = false;
        }
    }
    pthread_mutex_unlock(&s_mutex);
}
