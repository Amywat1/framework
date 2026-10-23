/**
 * @file    op_mode_bridge.c
 * @brief   运行模式事件桥接实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/bridges/op_mode_bridge.h"

#include "application/job_admit.h"
#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/event_bus/event_bus.h"

static void on_job_completed(const event_t *evt)
{
    job_admit_on_completed((uint64_t)evt->param);
}

static void on_job_aborted(const event_t *evt)
{
    job_admit_on_job_aborted((uint64_t)evt->param);
}

static void on_area_cleared(const event_t *evt)
{
    (void)evt;
    op_mode_on_area_cleared();
}

static void on_alarm_triggered(const event_t *evt)
{
    if (op_mode_alarm_port_is_estop(evt->param)) {
        return;
    }
    if (alarm_registry_has_blocking_active()) {
        op_mode_on_blocking_alarm();
    }
}

static void on_hw_estop_on(const event_t *evt)
{
    (void)evt;
    op_mode_on_estop(true);
}

static void on_hw_estop_off(const event_t *evt)
{
    (void)evt;
    op_mode_on_estop(false);
}

static void on_recovery_completed(const event_t *evt)
{
    op_mode_on_recovery_completed((recovery_result_t)evt->param);
}

static void on_self_check_completed(const event_t *evt)
{
    op_mode_on_self_check_completed(evt->param != 0U);
}

static void on_clear_done(const event_t *evt)
{
    (void)evt;
    op_mode_on_clear_done();
}

sw_err_t op_mode_bridge_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_JOB_COMPLETED,                on_job_completed       },
        {EVT_JOB_ABORTED,                  on_job_aborted         },
        {EVT_JOB_AREA_CLEARED,             on_area_cleared        },
        {EVT_ALARM_TRIGGERED,              on_alarm_triggered     },
        {EVT_HW_ESTOP_ON,                  on_hw_estop_on         },
        {EVT_HW_ESTOP_OFF,                 on_hw_estop_off        },
        {EVT_OP_MODE_RECOVERY_COMPLETED,   on_recovery_completed  },
        {EVT_OP_MODE_SELF_CHECK_COMPLETED, on_self_check_completed},
        {EVT_CLEAR_MACHINE_DONE,           on_clear_done          },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("op_mode_bridge: init ok");
    return SW_OK;
}
