/**
 * @file    snack_cloud_adapter.c
 * @brief   Snack MQTT 云端适配器（链路 + 属性上报 + 下行回复）
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    互斥量只保护凭证与 topic 缓冲。aliyun_mqtt_init / mqtt_connect / 发送
 *          放在锁外，避免 SDK 回调再入 is_online / publish / property_reply。
 *          换凭证时先在同一把锁里写入缓冲并暂停发送，再在锁外重建客户端。
 */

#include "adapters/outbound/cloud/providers/snack/snack_cloud_adapter.h"

#include "adapters/outbound/cloud/cloud_json.h"
#include "adapters/outbound/serialization/json/point_table_json.h"
#include "adapters/providers/snack/snack_sdk.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/cloud/cloud_point.h"
#include "runtime/event_bus/event_bus.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SNACK_CLOUD_CRED_MAX               64U
#define SNACK_CLOUD_TOPIC_MAX              128U
#define REPLY_JSON_BUF_SIZE                128U
#define SNACK_CLOUD_RECONNECT_INTERVAL_MS  5000U /**< 离线重连最小间隔，避免打爆平台 */

typedef struct {
    char     product_key[SNACK_CLOUD_CRED_MAX];
    char     device_sn[SNACK_CLOUD_CRED_MAX];
    char     device_secret[SNACK_CLOUD_CRED_MAX];
    char     iot_instance[SNACK_CLOUD_CRED_MAX];
    uint16_t heartbeat_s;
} snack_mqtt_cred_t;

static bool     s_initialized                         = false;
static bool     s_bootstrapped                        = false;
static bool     s_last_online                         = false;
static bool     s_tx_blocked                          = false;
static uint64_t s_last_reconnect_ms                   = 0U;
static char s_product_key[SNACK_CLOUD_CRED_MAX]   = "";
static char s_device_sn[SNACK_CLOUD_CRED_MAX]     = "";
static char s_device_secret[SNACK_CLOUD_CRED_MAX] = "";
static char s_iot_instance[SNACK_CLOUD_CRED_MAX]  = "";
static char s_topic_up[SNACK_CLOUD_TOPIC_MAX]     = "";
static char s_topic_reply[SNACK_CLOUD_TOPIC_MAX]  = "";
static uint16_t s_heartbeat_s                     = 0U;
static char s_report_json[CLOUD_REPORT_JSON_MAX];
static _Thread_local char s_send_json[CLOUD_REPORT_JSON_MAX];

static pthread_mutex_t s_mu;
static pthread_once_t  s_mu_once = PTHREAD_ONCE_INIT;

static void snack_cloud_mutex_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void snack_cloud_lock(void)
{
    (void)pthread_once(&s_mu_once, snack_cloud_mutex_init);
    pthread_mutex_lock(&s_mu);
}

static void snack_cloud_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

/**
 * @brief  查询 MQTT 在线。调用方必须已持锁，或确认此刻不会并发重建客户端
 */
static bool mqtt_online_unlocked(void)
{
    return mqtt_is_online() != 0;
}

static bool creds_ready_unlocked(void)
{
    return (s_product_key[0] != '\0') && (s_device_sn[0] != '\0') && (s_device_secret[0] != '\0');
}

static void copy_creds_unlocked(snack_mqtt_cred_t *out)
{
    (void)snprintf(out->product_key, sizeof(out->product_key), "%s", s_product_key);
    (void)snprintf(out->device_sn, sizeof(out->device_sn), "%s", s_device_sn);
    (void)snprintf(out->device_secret, sizeof(out->device_secret), "%s", s_device_secret);
    (void)snprintf(out->iot_instance, sizeof(out->iot_instance), "%s", s_iot_instance);
    out->heartbeat_s = s_heartbeat_s;
}

static bool link_is_online(void)
{
    bool online;

    snack_cloud_lock();
    online = mqtt_online_unlocked();
    snack_cloud_unlock();
    return online;
}

static void publish_connection_event(bool connected)
{
    if (connected) {
        (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
        LOG_INFO("snack_cloud: connected");
    } else {
        (void)event_publish(EVT_CLOUD_DISCONNECTED, 0U);
        LOG_WARN("snack_cloud: disconnected");
    }
}

/**
 * @brief  锁外重建 MQTT，再锁内提交会话；连接边沿在锁外发出
 */
static sw_err_t finish_rebuild(snack_mqtt_cred_t *cred, bool was_bootstrapped, const char *op)
{
    bool online;
    bool emit_connected;
    bool emit_disconnected;
    int  rc;

    rc = aliyun_mqtt_init(cred->product_key, cred->device_sn, cred->device_secret, cred->iot_instance,
                          (int)cred->heartbeat_s);
    if (rc == 0) {
        LOG_INFO("snack_cloud: mqtt %s sn=%s instance=%s", op, cred->device_sn, cred->iot_instance);
    } else {
        LOG_WARN("snack_cloud: mqtt %s offline sn=%s instance=%s", op, cred->device_sn, cred->iot_instance);
    }

    snack_cloud_lock();
    s_initialized = true;
    s_tx_blocked  = false;
    online        = mqtt_online_unlocked();
    /* 失败后再立刻 mqtt_connect 会删掉刚建的 aiot，测试三元组时会把进程弄死 */
    if (online) {
        s_last_reconnect_ms = 0U;
    } else {
        s_last_reconnect_ms = time_util_get_ms();
    }
    if (!was_bootstrapped) {
        s_bootstrapped    = true;
        s_last_online     = online;
        emit_disconnected = false;
        emit_connected    = online;
    } else {
        s_last_online     = online;
        emit_disconnected = true;
        emit_connected    = online;
    }
    snack_cloud_unlock();

    if (emit_disconnected) {
        publish_connection_event(false);
    }
    if (emit_connected) {
        publish_connection_event(true);
    }
    if (!was_bootstrapped) {
        LOG_INFO("snack_cloud: bootstrap ok");
    }
    return online ? SW_OK : SW_ERR_COMM;
}

static sw_err_t link_init(void)
{
    snack_mqtt_cred_t cred;
    bool              already;
    bool              was_bootstrapped;
    bool              first = false;
    bool              online = false;

    snack_cloud_lock();
    if (!creds_ready_unlocked()) {
        snack_cloud_unlock();
        return SW_ERR_NOT_INIT;
    }

    already = s_initialized;
    if (already) {
        online = mqtt_online_unlocked();
        if (!s_bootstrapped) {
            s_bootstrapped = true;
            s_last_online  = online;
            first          = true;
        }
        snack_cloud_unlock();
        if (first && online) {
            publish_connection_event(true);
        }
        if (first) {
            LOG_INFO("snack_cloud: bootstrap ok");
        }
        return online ? SW_OK : SW_ERR_COMM;
    }

    copy_creds_unlocked(&cred);
    was_bootstrapped = s_bootstrapped;
    s_tx_blocked     = true;
    snack_cloud_unlock();
    return finish_rebuild(&cred, was_bootstrapped, "init");
}

/**
 * @brief  暂停发送、拷出当前凭证，锁外重建客户端
 */
static sw_err_t rebuild_mqtt_session(const char *op)
{
    snack_mqtt_cred_t cred;
    bool              was_bootstrapped;

    snack_cloud_lock();
    if (!creds_ready_unlocked()) {
        snack_cloud_unlock();
        return SW_ERR_NOT_INIT;
    }
    copy_creds_unlocked(&cred);
    was_bootstrapped    = s_bootstrapped;
    s_tx_blocked        = true;
    s_last_reconnect_ms = 0U;
    snack_cloud_unlock();
    return finish_rebuild(&cred, was_bootstrapped, op);
}

sw_err_t snack_cloud_adapter_rebind(void)
{
    return rebuild_mqtt_session("rebind");
}

/**
 * @brief  离线时按间隔重建 MQTT 客户端并连接
 */
static void try_reconnect(void)
{
    uint64_t now_ms;
    char     sn[SNACK_CLOUD_CRED_MAX];

    snack_cloud_lock();
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return;
    }
    now_ms = time_util_get_ms();
    if ((s_last_reconnect_ms != 0U)
        && (time_elapsed_ms(s_last_reconnect_ms, now_ms) < SNACK_CLOUD_RECONNECT_INTERVAL_MS)) {
        snack_cloud_unlock();
        return;
    }
    s_last_reconnect_ms = now_ms;
    (void)snprintf(sn, sizeof(sn), "%s", s_device_sn);
    snack_cloud_unlock();

    LOG_WARN("snack_cloud: retry connect sn=%s", sn);
    if (mqtt_connect() == 0) {
        snack_cloud_lock();
        if (!s_tx_blocked) {
            s_last_reconnect_ms = 0U;
        }
        snack_cloud_unlock();
    }
}

static void link_poll(void)
{
    bool now_online;
    bool changed = false;
    bool blocked;
    bool need_reconn;

    snack_cloud_lock();
    blocked     = s_tx_blocked;
    need_reconn = !blocked && s_initialized && !mqtt_online_unlocked();
    snack_cloud_unlock();

    if (!blocked) {
        cloud_json_poll();
    }
    if (need_reconn) {
        try_reconnect();
    }

    snack_cloud_lock();
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return;
    }
    now_online = mqtt_online_unlocked();
    if (now_online != s_last_online) {
        s_last_online = now_online;
        changed       = true;
    }
    snack_cloud_unlock();

    if (changed) {
        publish_connection_event(now_online);
    }
}

static sw_err_t link_publish(const char *topic, const char *payload)
{
    if ((topic == NULL) || (payload == NULL)) {
        return SW_ERR_PARAM;
    }

    snack_cloud_lock();
    if (s_tx_blocked || !mqtt_online_unlocked()) {
        snack_cloud_unlock();
        return SW_ERR_COMM;
    }
    snack_cloud_unlock();

    if (net_mqtt_send((char *)topic, (char *)payload) != 0) {
        return SW_ERR_COMM;
    }
    return SW_OK;
}

static void link_set_recv_handler(cloud_link_recv_fn_t cb)
{
    mqtt_recv_handler_set((mqtt_recv_handler_t)cb);
}

static sw_err_t send_json(const char *json)
{
    char topic[SNACK_CLOUD_TOPIC_MAX];

    snack_cloud_lock();
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return SW_ERR_COMM;
    }
    if (s_topic_up[0] == '\0') {
        snack_cloud_unlock();
        return SW_ERR_PARAM;
    }
    if ((json == NULL) || (json[0] == '\0') || (strcmp(json, "{}") == 0)) {
        snack_cloud_unlock();
        return SW_OK;
    }
    (void)snprintf(topic, sizeof(topic), "%s", s_topic_up);
    snack_cloud_unlock();
    return link_publish(topic, json);
}

/**
 * @brief  锁内把共享 JSON 拷到本线程发送缓冲，再解锁发出
 * @note   调用方必须已持锁；返回时锁已释放
 */
static sw_err_t copy_report_and_send(void)
{
    (void)memcpy(s_send_json, s_report_json, sizeof(s_send_json));
    snack_cloud_unlock();
    return send_json(s_send_json);
}

static sw_err_t adapter_publish_properties(void)
{
    sw_err_t ret;

    snack_cloud_lock();
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return SW_ERR_COMM;
    }
    ret = cloud_json_build_properties(s_report_json, sizeof(s_report_json));
    if (ret == SW_OK) {
        return copy_report_and_send();
    }
    if (ret != SW_ERR_OVERFLOW) {
        LOG_ERROR("snack_cloud: properties build failed");
        snack_cloud_unlock();
        return ret;
    }
    snack_cloud_unlock();
    return cloud_json_publish_snapshot(true);
}

static sw_err_t adapter_publish_properties_delta(const char *const *ids, size_t count)
{
    sw_err_t ret;

    if ((ids == NULL) || (count == 0U)) {
        return SW_ERR_PARAM;
    }

    snack_cloud_lock();
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return SW_ERR_COMM;
    }
    ret = cloud_json_build_properties_delta(ids, count, s_report_json, sizeof(s_report_json));
    if (ret == SW_OK) {
        return copy_report_and_send();
    }
    if (ret != SW_ERR_OVERFLOW) {
        LOG_ERROR("snack_cloud: delta build failed");
        snack_cloud_unlock();
        return ret;
    }
    snack_cloud_unlock();
    return cloud_json_publish_delta(ids, count);
}

static sw_err_t adapter_publish_properties_json(const char *json)
{
    if ((json == NULL) || (json[0] == '\0')) {
        return SW_ERR_PARAM;
    }
    return send_json(json);
}

sw_err_t snack_cloud_property_reply(const char *request_json, const point_apply_result_t *result)
{
    char   topic[SNACK_CLOUD_TOPIC_MAX];
    char   buf[REPLY_JSON_BUF_SIZE];
    size_t applied  = 0U;
    size_t rejected = 0U;

    (void)request_json;

    snack_cloud_lock();
    if (s_topic_reply[0] == '\0') {
        snack_cloud_unlock();
        return SW_OK;
    }
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return SW_ERR_COMM;
    }
    (void)snprintf(topic, sizeof(topic), "%s", s_topic_reply);
    snack_cloud_unlock();

    if (result != NULL) {
        applied  = result->applied;
        rejected = result->rejected;
    }

    (void)snprintf(buf, sizeof(buf), "{\"applied\":%u,\"rejected\":%u}", (unsigned)applied, (unsigned)rejected);

    if (link_publish(topic, buf) != SW_OK) {
        LOG_WARN("snack_cloud: reply send failed");
        return SW_ERR_COMM;
    }
    return SW_OK;
}

static sw_err_t ota_report(const char *version)
{
    snack_cloud_lock();
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return SW_ERR_COMM;
    }
    snack_cloud_unlock();
    return (snack_ota_report(version) == 0) ? SW_OK : SW_ERR_COMM;
}

static bool ota_busy(void)
{
    return snack_ota_busy();
}

static const ota_channel_ops_t s_ota_channel = {
    .report = ota_report,
    .busy   = ota_busy,
};

const ota_channel_ops_t *snack_cloud_ota_channel(void)
{
    return &s_ota_channel;
}

sw_err_t snack_cloud_ota_write_sidecar(const char *bundle_path)
{
    snack_cloud_lock();
    if (s_tx_blocked) {
        snack_cloud_unlock();
        return SW_ERR_STATE;
    }
    snack_cloud_unlock();
    return (snack_ota_write_job_sidecar(bundle_path) == 0) ? SW_OK : SW_ERR_STATE;
}

static const cloud_link_ops_t s_ops = {
    .init                     = link_init,
    .is_online                = link_is_online,
    .poll                     = link_poll,
    .publish                  = link_publish,
    .publish_properties       = adapter_publish_properties,
    .publish_properties_delta = adapter_publish_properties_delta,
    .publish_properties_json  = adapter_publish_properties_json,
    .set_recv_handler         = link_set_recv_handler,
};

void snack_cloud_adapter_register(void)
{
    snack_cloud_lock();
    s_initialized       = false;
    s_bootstrapped      = false;
    s_last_online       = false;
    s_tx_blocked        = false;
    s_last_reconnect_ms = 0U;
    snack_cloud_unlock();
    point_table_json_set_bool_as_number(true);
    cloud_json_set_pulse_echo_hold_ms(500U);
    cloud_link_register(&s_ops);
    LOG_INFO("snack_cloud: adapter registered");
}

/**
 * @brief  写入凭证与 topic。调用方必须已持锁
 */
static sw_err_t configure_unlocked(const char *product_key, const char *device_sn, const char *device_secret,
                                   const char *iot_instance, const char *topic_property_up,
                                   const char *topic_property_reply, uint16_t heartbeat_s)
{
    if ((product_key == NULL) || (device_sn == NULL) || (device_secret == NULL) || (topic_property_up == NULL)
        || (product_key[0] == '\0') || (device_sn[0] == '\0') || (device_secret[0] == '\0')
        || (topic_property_up[0] == '\0') || (heartbeat_s == 0U)) {
        return SW_ERR_PARAM;
    }
    if ((strlen(product_key) >= sizeof(s_product_key)) || (strlen(device_sn) >= sizeof(s_device_sn))
        || (strlen(device_secret) >= sizeof(s_device_secret)) || (strlen(topic_property_up) >= sizeof(s_topic_up))) {
        return SW_ERR_PARAM;
    }
    if ((iot_instance != NULL) && (strlen(iot_instance) >= sizeof(s_iot_instance))) {
        return SW_ERR_PARAM;
    }
    if ((topic_property_reply != NULL) && (topic_property_reply[0] != '\0')
        && (strlen(topic_property_reply) >= sizeof(s_topic_reply))) {
        return SW_ERR_PARAM;
    }

    (void)snprintf(s_product_key, sizeof(s_product_key), "%s", product_key);
    (void)snprintf(s_device_sn, sizeof(s_device_sn), "%s", device_sn);
    (void)snprintf(s_device_secret, sizeof(s_device_secret), "%s", device_secret);
    if ((iot_instance == NULL) || (iot_instance[0] == '\0')) {
        s_iot_instance[0] = '\0';
    } else {
        (void)snprintf(s_iot_instance, sizeof(s_iot_instance), "%s", iot_instance);
    }
    (void)snprintf(s_topic_up, sizeof(s_topic_up), "%s", topic_property_up);
    s_heartbeat_s = heartbeat_s;

    if ((topic_property_reply == NULL) || (topic_property_reply[0] == '\0')) {
        s_topic_reply[0] = '\0';
    } else {
        (void)snprintf(s_topic_reply, sizeof(s_topic_reply), "%s", topic_property_reply);
    }

    if (s_initialized) {
        s_tx_blocked = true;
    }
    return SW_OK;
}

sw_err_t snack_cloud_adapter_configure(const char *product_key, const char *device_sn, const char *device_secret,
                                       const char *iot_instance, const char *topic_property_up,
                                       const char *topic_property_reply, uint16_t heartbeat_s)
{
    sw_err_t ret;

    snack_cloud_lock();
    ret = configure_unlocked(product_key, device_sn, device_secret, iot_instance, topic_property_up,
                             topic_property_reply, heartbeat_s);
    snack_cloud_unlock();
    return ret;
}

sw_err_t snack_cloud_adapter_apply(const char *product_key, const char *device_sn, const char *device_secret,
                                   const char *iot_instance, const char *topic_property_up,
                                   const char *topic_property_reply, uint16_t heartbeat_s)
{
    snack_mqtt_cred_t cred;
    bool              was_bootstrapped;
    sw_err_t          ret;

    snack_cloud_lock();
    ret = configure_unlocked(product_key, device_sn, device_secret, iot_instance, topic_property_up,
                             topic_property_reply, heartbeat_s);
    if (ret != SW_OK) {
        snack_cloud_unlock();
        return ret;
    }
    if (!creds_ready_unlocked()) {
        snack_cloud_unlock();
        return SW_ERR_NOT_INIT;
    }
    copy_creds_unlocked(&cred);
    was_bootstrapped    = s_bootstrapped;
    s_tx_blocked        = true;
    s_last_reconnect_ms = 0U;
    snack_cloud_unlock();
    return finish_rebuild(&cred, was_bootstrapped, "rebind");
}
