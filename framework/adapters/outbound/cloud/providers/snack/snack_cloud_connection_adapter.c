/**
 * @file    snack_cloud_connection_adapter.c
 * @brief   Snack MQTT 云端连接状态适配器实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_connection_adapter.h"
#include "framework/ports/outbound/cloud/connection/connection_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/scheduler/periodic_task.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/adapters/runtime/snack/snack_mqtt.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include <sched.h>
#include <stdbool.h>

#define CLOUD_CONN_POLL_MS   500U

static bool s_last_connected = false;
static bool s_started        = false;

static bool adapter_is_connected(void)
{
    return mqtt_is_online() != 0;
}

static void publish_connection_event(bool connected)
{
    if (connected)
    {
        (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
        LOG_INFO("snack_cloud_conn: connected");
    }
    else
    {
        (void)event_publish(EVT_CLOUD_DISCONNECTED, 0U);
        LOG_WARN("snack_cloud_conn: disconnected");
    }
}

static void connection_poll_cb(void *ctx)
{
    bool now_connected;

    (void)ctx;
    now_connected = adapter_is_connected();

    if (now_connected == s_last_connected)
    {
        return;
    }

    s_last_connected = now_connected;
    publish_connection_event(now_connected);
}

static const cloud_connection_ops_t s_ops = {
    .is_connected = adapter_is_connected,
};

void snack_cloud_connection_adapter_register(void)
{
    cloud_connection_register(&s_ops);
    LOG_INFO("snack_cloud_conn: adapter registered");
}

sw_err_t snack_cloud_connection_adapter_start(void)
{
    sw_err_t ret;

    if (s_started)
    {
        return SW_OK;
    }
    s_started = true;

    s_last_connected = adapter_is_connected();
    if (s_last_connected)
    {
        publish_connection_event(true);
    }

    ret = periodic_task_register("cloud_conn",
                                 CLOUD_CONN_POLL_MS,
                                 connection_poll_cb,
                                 NULL,
                                 SCHED_OTHER,
                                 THD_CLOUD_NICE,
                                 THD_CLOUD_STACK);
    if (ret != SW_OK)
    {
        LOG_ERROR("snack_cloud_conn: periodic task register failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("snack_cloud_conn: monitor started");
    return SW_OK;
}
