/**
 * @file    emergency_handler.c
 * @brief   安全 LOCKOUT 与急停释放后的归位协调
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    阶段二职责划分（§6.5 / §7.3）：
 *
 *   safety_thread（SCHED_FIFO）
 *     → 硬件急停边沿检测、device_stop_all_actuators()、发布 EVT_HW_ESTOP_ON/OFF
 *
 *   EVT_HW_ESTOP_ON
 *     → safety_deferred_stop()（领域收敛 + 可选 flush）
 *     → op_mode_on_estop_triggered() 由 op_mode_bridge 消费，不在此模块处理
 *
 *   EVT_HW_ESTOP_OFF
 *     → 执行安全归位（刷子回零），发布 EVT_SAFETY_HOME_DONE
 *
 *   EVT_SAFETY_LOCKOUT（报警聚合 CRITICAL，非急停硬件路径）
 *     → wash_orchestrator_abort(WASH_ABORT_CRITICAL)
 *     → 若非急停硬件激活：立即安全归位；否则推迟到 EVT_HW_ESTOP_OFF
 */

#include "framework/application/orchestrators/emergency_handler.h"
#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/application/safety_deferred_stop.h"
#include "framework/domain/command_gateway/op_mode_types.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/ports/outbound/safety/hw_estop_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include "framework/common/sw_error.h"

static void do_safety_home(void)
{
    (void)brush_stop_all();
    (void)event_publish(EVT_SAFETY_HOME_DONE, (uint32_t)SW_OK);
    LOG_INFO("emergency_handler: safety home done");
}

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    safety_deferred_stop();
    LOG_WARN("emergency_handler: EVT_HW_ESTOP_ON received, safety home deferred until release");
}

static void on_estop_off(const event_t *evt)
{
    (void)evt;
    LOG_INFO("emergency_handler: EVT_HW_ESTOP_OFF, executing safety home");
    do_safety_home();
}

static void on_safety_lockout(const event_t *evt)
{
    (void)evt;

    wash_orchestrator_abort(WASH_ABORT_CRITICAL);

    if (hw_estop_port_is_active())
    {
        LOG_WARN("emergency_handler: LOCKOUT during HW ESTOP, wash aborted, home deferred");
        return;
    }

    LOG_WARN("emergency_handler: LOCKOUT, wash aborted, executing safety home");
    do_safety_home();
}

sw_err_t emergency_handler_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_HW_ESTOP_ON,    on_estop_on      },
        { EVT_HW_ESTOP_OFF,   on_estop_off     },
        { EVT_SAFETY_LOCKOUT, on_safety_lockout },
    };

    return event_subscribe_table(s_subs,
                                 sizeof(s_subs) / sizeof(s_subs[0]));
}
