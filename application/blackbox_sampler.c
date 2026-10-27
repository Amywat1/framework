/**
 * @file    blackbox_sampler.c
 * @brief   可选默认通道采样与显式触发表
 */

#include "application/blackbox_sampler.h"

#include "common/event_types.h"
#include "domain/telemetry/device_snapshot.h"
#include "observability/recorder/blackbox_recorder.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>
#include <stdbool.h>

#define BLACKBOX_SAMPLER_PERIOD_MS (1000U / BLACKBOX_SAMPLER_HZ)

/** 快照精简包：模式 / 急停 / 阻断 / 最高报警 / 排队 / 在洗 / 入场相位 */
typedef struct {
    uint8_t  mode;
    uint8_t  estop;
    uint8_t  blocking;
    uint32_t top_alarm;
    uint8_t  queued;
    uint8_t  active;
    uint8_t  occ_phase;
} blackbox_snap_pack_t;

static uint32_t s_trig_mask;
static uint64_t s_next_incident;
static bool     s_started;

static void freeze(uint32_t trigger_code)
{
    s_next_incident++;
    (void)blackbox_recorder_trigger(s_next_incident, trigger_code);
}

static void on_trigger_event(const event_t *evt)
{
    uint32_t bit;

    if (evt == NULL) {
        return;
    }
    if (evt->type == EVT_HW_ESTOP_ON) {
        bit = BLACKBOX_TRIG_ESTOP;
    } else if (evt->type == EVT_SAFETY_LOCKOUT) {
        bit = BLACKBOX_TRIG_LOCKOUT;
    } else {
        return;
    }
    if ((s_trig_mask & bit) != 0U) {
        freeze((uint32_t)evt->type);
    }
}

void blackbox_sampler_tick(void)
{
    device_snapshot_t          snap = device_snapshot_get();
    const blackbox_snap_pack_t pack = {
        .mode      = (uint8_t)snap.op.mode,
        .estop     = snap.op.estop_active ? 1U : 0U,
        .blocking  = snap.safety.blocking_active ? 1U : 0U,
        .top_alarm = snap.safety.top_alarm_code,
        .queued    = (uint8_t)snap.wash.queued_count,
        .active    = (uint8_t)snap.wash.active_count,
        .occ_phase = (uint8_t)snap.occupancy.phase,
    };

    (void)blackbox_recorder_record(1U, &pack, sizeof(pack));
}

static void sampler_tick_fn(void *ctx)
{
    (void)ctx;
    blackbox_sampler_tick();
}

sw_err_t blackbox_sampler_start(uint16_t pre_seconds, uint16_t post_seconds, uint32_t triggers)
{
    const blackbox_config_t cfg = {
        .sample_rate_hz       = (uint16_t)BLACKBOX_SAMPLER_HZ,
        .pre_trigger_seconds  = pre_seconds,
        .post_trigger_seconds = post_seconds,
    };
    event_subscription_t subs[2];
    size_t               n = 0U;
    sw_err_t             ret;

    if (s_started) {
        return SW_OK;
    }

    ret = blackbox_recorder_init(&cfg);
    if (ret != SW_OK) {
        return ret;
    }

    s_trig_mask     = triggers;
    s_next_incident = 0U;

    if ((triggers & BLACKBOX_TRIG_ESTOP) != 0U) {
        subs[n].type    = EVT_HW_ESTOP_ON;
        subs[n].handler = on_trigger_event;
        n++;
    }
    if ((triggers & BLACKBOX_TRIG_LOCKOUT) != 0U) {
        subs[n].type    = EVT_SAFETY_LOCKOUT;
        subs[n].handler = on_trigger_event;
        n++;
    }
    if (n > 0U) {
        ret = event_subscribe_table(subs, n);
        if (ret != SW_OK) {
            return ret;
        }
    }

    ret = periodic_task_register("blackbox_sampler",
                                 BLACKBOX_SAMPLER_PERIOD_MS,
                                 sampler_tick_fn,
                                 NULL,
                                 SCHED_OTHER,
                                 0,
                                 THD_SENSOR_POLL_STACK);
    if (ret != SW_OK) {
        return ret;
    }
    s_started = true;
    return SW_OK;
}

void blackbox_sampler_on_watchdog_trip(void)
{
    if ((s_trig_mask & BLACKBOX_TRIG_WATCHDOG) != 0U) {
        freeze(BLACKBOX_TRIG_WATCHDOG);
    }
}

void blackbox_sampler_reset_for_test(void)
{
    s_trig_mask     = 0U;
    s_next_incident = 0U;
    s_started       = false;
}
