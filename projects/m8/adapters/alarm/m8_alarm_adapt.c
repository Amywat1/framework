/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警绑定适配实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/adapters/alarm/m8_alarm_adapt.h"
#include "projects/m8/adapters/alarm/m8_comm_watchdog.h"
#include "framework/application/alarm_event_bridge.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "projects/m8/config/m8_alarm_table.h"
#include "framework/common/log.h"
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#define ALARM_POLL_PERIOD_MS   30U
#define ALARM_POLL_STACK_SIZE  (16U * 1024U)

typedef enum
{
#define X(pin, al, tr, rl, cls, idx, nat, lvl, resp, clr, sk, sc, cut, desc) ALARM_IDX_##pin,
    M8_HW_ALARM_TABLE(X)
#undef X
    M8_HW_ALARM_COUNT
} m8_alarm_idx_t;

typedef struct
{
    io_di_t  pin;
    bool     active_low;
    uint8_t  trig_max;
    uint8_t  rel_max;
    uint32_t code;
} alarm_src_cfg_t;

static const alarm_src_cfg_t s_cfg[] = {
#define X(pin, al, tr, rl, cls, idx, nat, lvl, resp, clr, sk, sc, cut, desc) \
    { (pin), (al), (tr), (rl), ALARM_CODE_MAKE(cls, idx, nat) },
    M8_HW_ALARM_TABLE(X)
#undef X
};

typedef struct
{
    uint8_t trig_cnt;
    uint8_t rel_cnt;
    bool    active;
} alarm_filter_t;

static alarm_filter_t s_filter[M8_HW_ALARM_COUNT];

static bool debounced_active(const alarm_filter_t *f, const alarm_src_cfg_t *cfg, bool raw)
{
    if (raw)
    {
        return (f->trig_cnt >= cfg->trig_max);
    }
    if (f->active)
    {
        return (f->rel_cnt < cfg->rel_max);
    }
    return false;
}

sw_err_t m8_alarm_adapt_init(void)
{
    const hal_io_ops_t        *io  = hal_io_get_ops();
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    int                        i;

    for (i = 0; i < (int)M8_HW_ALARM_COUNT; ++i)
    {
        bool raw = ((io != NULL) && (io->di_read != NULL))
                   ? (io->di_read(s_cfg[i].pin) ^ s_cfg[i].active_low)
                   : false;

        if (raw)
        {
            s_filter[i].trig_cnt = s_cfg[i].trig_max;
            s_filter[i].rel_cnt  = 0U;
            s_filter[i].active   = false;
            if ((ops != NULL) && (ops->trigger != NULL))
            {
                (void)ops->trigger(s_cfg[i].code);
                s_filter[i].active = true;
            }
        }
        else
        {
            s_filter[i].trig_cnt = 0U;
            s_filter[i].rel_cnt  = s_cfg[i].rel_max;
            s_filter[i].active   = false;
        }
    }

    LOG_INFO("m8_alarm_adapt: init ok, sources=%d", (int)M8_HW_ALARM_COUNT);
    return SW_OK;
}

void m8_alarm_adapt_poll(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    const hal_io_ops_t        *io  = hal_io_get_ops();
    int                        i;

    if ((ops == NULL) || (io == NULL) || (io->di_read == NULL))
    {
        return;
    }

    for (i = 0; i < (int)M8_HW_ALARM_COUNT; ++i)
    {
        bool            raw = io->di_read(s_cfg[i].pin) ^ s_cfg[i].active_low;
        alarm_filter_t *f   = &s_filter[i];
        bool            debounced;

        if (raw)
        {
            if (f->trig_cnt < s_cfg[i].trig_max)
            {
                f->trig_cnt++;
            }
            f->rel_cnt = 0U;
        }
        else
        {
            if (f->rel_cnt < s_cfg[i].rel_max)
            {
                f->rel_cnt++;
            }
            f->trig_cnt = 0U;
        }

        debounced = debounced_active(f, &s_cfg[i], raw);
        if (debounced == f->active)
        {
            continue;
        }

        f->active = debounced;
        if (debounced)
        {
            (void)ops->trigger(s_cfg[i].code);
        }
        else
        {
            (void)ops->clear(s_cfg[i].code);
        }
    }
}

static void *alarm_poll_thread_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        m8_alarm_adapt_poll();
        m8_comm_watchdog_poll();
        alarm_event_bridge_drain();
        usleep((unsigned long)ALARM_POLL_PERIOD_MS * 1000UL);
    }

    return NULL;
}

sw_err_t m8_alarm_adapt_poll_start(void)
{
    pthread_attr_t attr;
    pthread_t      tid;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, ALARM_POLL_STACK_SIZE);

    if (pthread_create(&tid, &attr, alarm_poll_thread_fn, NULL) != 0)
    {
        pthread_attr_destroy(&attr);
        LOG_ERROR("m8_alarm_adapt_poll_start: pthread_create failed");
        return SW_ERR_HW;
    }

    pthread_attr_destroy(&attr);
    pthread_detach(tid);
    LOG_INFO("m8_alarm_adapt_poll_start: alarm_poll thread started");
    return SW_OK;
}
