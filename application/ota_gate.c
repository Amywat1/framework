/**
 * @file    ota_gate.c
 * @brief   远程升级门闩实现
 *
 * @note    状态只在事件分发线程与启动期 init 中改写；ota_gate_blocks_start 只读绑定指针，
 *          绑定在调度启动前完成。
 */

#include "application/ota_gate.h"

#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/event_types.h"
#include "common/log.h"
#include "domain/op_mode/operational_mode.h"
#include "runtime/event_bus/event_bus.h"

#include <stddef.h>

static const ota_gate_cfg_t *s_cfg;
static bool                  s_events_bound;
static bool                  s_reported;

static bool channel_valid(const ota_channel_ops_t *channel, const char *version)
{
    if (channel == NULL) {
        return true;
    }
    return (channel->report != NULL) && (version != NULL) && (version[0] != '\0');
}

static bool installer_valid(const ota_installer_ops_t *installer)
{
    if (installer == NULL) {
        return true;
    }
    return (installer->set_allowed != NULL) && (installer->is_installing != NULL);
}

sw_err_t ota_gate_bind(const ota_gate_cfg_t *cfg)
{
    if ((cfg != NULL) &&
        (!channel_valid(cfg->channel, cfg->version) || !installer_valid(cfg->installer))) {
        return SW_ERR_PARAM;
    }
    s_cfg      = cfg;
    s_reported = false;
    return SW_OK;
}

bool ota_gate_is_allowed(operational_mode_t mode, bool estop_active)
{
    if (estop_active) {
        return false;
    }
    return (mode == OP_MODE_STANDBY) || (mode == OP_MODE_STOPPED);
}

bool ota_gate_blocks_start(void)
{
    const ota_gate_cfg_t *cfg = s_cfg;

    if (cfg == NULL) {
        return false;
    }
    if ((cfg->channel != NULL) && (cfg->channel->busy != NULL) && cfg->channel->busy()) {
        return true;
    }
    return (cfg->installer != NULL) && cfg->installer->is_installing();
}

static bool cloud_is_online(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    return (ops != NULL) && (ops->is_online != NULL) && ops->is_online();
}

void ota_gate_sync(void)
{
    const ota_gate_cfg_t *cfg = s_cfg;
    bool                  allow;
    sw_err_t              ret;

    if (cfg == NULL) {
        return;
    }
    allow = ota_gate_is_allowed(op_mode_get_current(), op_mode_is_estop_active());
    if (cfg->installer != NULL) {
        cfg->installer->set_allowed(allow);
    }
    if (!allow) {
        s_reported = false;
        return;
    }
    if (s_reported || (cfg->channel == NULL) || !cloud_is_online()) {
        return;
    }
    ret = cfg->channel->report(cfg->version);
    if (ret == SW_OK) {
        s_reported = true;
        LOG_INFO("ota_gate: reported version=%s", cfg->version);
    } else {
        LOG_WARN("ota_gate: report failed ret=%d version=%s", (int)ret, cfg->version);
    }
}

static void on_gate_event(const event_t *evt)
{
    (void)evt;
    ota_gate_sync();
}

sw_err_t ota_gate_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_CLOUD_CONNECTED,      on_gate_event},
        {EVT_CLOUD_DISCONNECTED,   on_gate_event},
        {EVT_OP_MODE_CHANGED,      on_gate_event},
        {EVT_OP_MODE_CONTEXT_SYNC, on_gate_event},
    };
    sw_err_t ret;

    if (!s_events_bound) {
        ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
        if (ret != SW_OK) {
            return ret;
        }
        s_events_bound = true;
    }
    ota_gate_sync();
    return SW_OK;
}

void ota_gate_reset_for_test(void)
{
    s_cfg          = NULL;
    s_events_bound = false;
    s_reported     = false;
}
