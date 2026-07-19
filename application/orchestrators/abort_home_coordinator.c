/**
 * @file    abort_home_coordinator.c
 * @brief   中止归位协调实现
 * @author  HUWANGWEI
 * @date    2026-07-19
 *
 * @note    EVT_ABORT_HOME_REQUESTED → machine_ops.abort_home()（仅启动）。
 *          完成事件 EVT_ABORT_HOME_DONE 由项目在运动结束后发布。
 */

#include "application/orchestrators/abort_home_coordinator.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"

static void on_abort_home_requested(const event_t *evt)
{
    const machine_ops_t *ops = machine_ops_get();

    (void)evt;
    LOG_INFO("abort_home: requested, starting machine_ops.abort_home");
    if ((ops != NULL) && (ops->abort_home != NULL)) {
        ops->abort_home();
    } else {
        LOG_ERROR("abort_home: ops.abort_home missing, publish DONE fail");
        (void)event_publish(EVT_ABORT_HOME_DONE, (uint32_t)SW_ERR_NOT_INIT);
    }
}

sw_err_t abort_home_coordinator_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_ABORT_HOME_REQUESTED, on_abort_home_requested},
    };

    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
