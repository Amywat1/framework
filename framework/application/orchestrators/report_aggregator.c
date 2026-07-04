/**
 * @file    report_aggregator.c
 * @brief   云端状态上报聚合器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/application/orchestrators/report_aggregator.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include <unistd.h>
#include <sched.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 内部：聚合 cloud_report_payload_t
 * ------------------------------------------------------------------------- */
static void build_payload(cloud_report_payload_t *p)
{
    device_context_t ctx = dev_ctx_snapshot();

    memset(p, 0, sizeof(*p));
    p->dev_state      = (uint8_t)ctx.device_state;
    p->wash_mode      = (uint8_t)ctx.wash_mode;
    p->gantry_pos     = ctx.gantry_pos;
    p->cloud_connected = ctx.cloud_connected;
    p->safety_state   = (uint8_t)ctx.safety_state;
    p->has_alarm      = ctx.has_alarm;
    p->alarm_code     = ctx.alarm_code;
}

/* -------------------------------------------------------------------------
 * 上报一次（供周期线程与 report_aggregator_request_resync 共用）
 * ------------------------------------------------------------------------- */
static void report_once(void)
{
    const cloud_report_ops_t *ops = cloud_report_get_ops();

    if ((ops == NULL) || !ops->is_connected())
    {
        return; /* 未注册或离线：不发送，避免无用错误日志 */
    }

    cloud_report_payload_t payload;
    build_payload(&payload);

    sw_err_t ret = ops->report(&payload);
    if ((ret != SW_OK) && (ret != SW_ERR_COMM))
    {
        LOG_WARN("report_aggregator: report failed ret=%d", (int)ret);
    }
}

/* -------------------------------------------------------------------------
 * cloud_thread 函数（周期性上报）
 * ------------------------------------------------------------------------- */
static void *cloud_thread_fn(void *arg)
{
    (void)arg;
    bool s_last_connected = false;

    while (true)
    {
        usleep((unsigned long)THD_CLOUD_REPORT_PERIOD_MS * 1000UL);

        const cloud_report_ops_t *ops = cloud_report_get_ops();
        if (ops == NULL)
        {
            /* 云端适配器尚未注册，静默等待 */
            continue;
        }

        /* 检测连接状态变化，补全 DISCONNECTED 事件生产者 */
        bool now_connected = ops->is_connected();
        if (now_connected != s_last_connected)
        {
            s_last_connected = now_connected;
            if (now_connected)
            {
                (void)event_publish(EVT_CLOUD_CONNECTED,    0U);
            }
            else
            {
                (void)event_publish(EVT_CLOUD_DISCONNECTED, 0U);
                LOG_WARN("report_aggregator: cloud disconnected");
            }
        }

        report_once();
    }

    return NULL;
}

void report_aggregator_request_resync(void)
{
    report_once();
}

/* -------------------------------------------------------------------------
 * 事件处理：云端连接状态变化
 * ------------------------------------------------------------------------- */
static void on_cloud_connected(const event_t *evt)
{
    (void)evt;
    dev_ctx_set_cloud_status(true);
    LOG_INFO("report_aggregator: cloud connected");
}

static void on_cloud_disconnected(const event_t *evt)
{
    (void)evt;
    dev_ctx_set_cloud_status(false);
    LOG_WARN("report_aggregator: cloud disconnected");
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t report_aggregator_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_CLOUD_CONNECTED,    on_cloud_connected    },
        { EVT_CLOUD_DISCONNECTED, on_cloud_disconnected },
    };
    sw_err_t ret;

    ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    ret = thread_register("cloud", cloud_thread_fn,
                          SCHED_OTHER, 0, THD_CLOUD_STACK);
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("report_aggregator: init ok");
    return SW_OK;
}
