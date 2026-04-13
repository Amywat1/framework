/**
 * @file    report_aggregator.c
 * @brief   云端状态上报聚合器实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/orchestrators/report_aggregator.h"
#include "core/scheduler/thread_registry.h"
#include "service/dev_ctx/dev_ctx.h"
#include "domain/device/gantry.h"
#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "ports/cloud/report_port.h"
#include "core/event_bus/event_bus.h"
#include "config/threading/thread_config.h"
#include "common/event_types.h"
#include "common/log.h"
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
    p->wash_step      = (uint8_t)ctx.wash_step;
    p->gantry_pos     = gantry_get_pos();
    p->has_alarm      = ctx.has_error_alarm;
    p->cloud_connected = ctx.cloud_connected;

    /* 填充当前最高优先级报警码（遍历已知码）*/
    static const uint16_t s_priority_order[] = {
        ALARM_CODE_ESTOP,
        ALARM_CODE_VFD_GANTRY, ALARM_CODE_VFD_BRUSH, ALARM_CODE_STEPPER_FAULT,
        ALARM_CODE_GANTRY_CURRENT, ALARM_CODE_BRUSH_CURRENT,
        ALARM_CODE_GANTRY_FWD_LIM, ALARM_CODE_GANTRY_REV_LIM,
        ALARM_CODE_LIFT_UP_LIM,    ALARM_CODE_LIFT_DOWN_LIM,
        ALARM_CODE_MODBUS_GANTRY,  ALARM_CODE_MODBUS_BRUSH,
        ALARM_CODE_MQTT_OFFLINE,
    };
    for (size_t i = 0; i < sizeof(s_priority_order) / sizeof(s_priority_order[0]); i++)
    {
        if (alarm_core_is_active(s_priority_order[i]))
        {
            p->alarm_code = s_priority_order[i];
            break;
        }
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

        if (!now_connected)
        {
            continue; /* 离线时不发送，避免无用错误日志 */
        }

        cloud_report_payload_t payload;
        build_payload(&payload);

        sw_err_t ret = ops->report(&payload);
        if ((ret != SW_OK) && (ret != SW_ERR_COMM))
        {
            LOG_WARN("report_aggregator: report failed ret=%d", (int)ret);
        }
    }

    return NULL;
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
    sw_err_t ret;

    ret = event_subscribe(EVT_CLOUD_CONNECTED,    on_cloud_connected);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_CLOUD_DISCONNECTED, on_cloud_disconnected);
    if (ret != SW_OK) { return ret; }

    ret = thread_register("cloud", cloud_thread_fn,
                          SCHED_OTHER, 0, THD_CLOUD_STACK);
    if (ret != SW_OK) { return ret; }

    LOG_INFO("report_aggregator: init ok");
    return SW_OK;
}
