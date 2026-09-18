/**
 * @file    snack_wrapper.cpp
 * @brief   snack SDK 封装实现（日志、阿里云 MQTT、音乐、BLE、CLI）
 * @author  HUWANGWEI
 * @date    2026-04-08
 */

#include "framework/adapters/providers/snack/snack_sdk.h"
#include "framework/common/log.h"
#include "log/mlog.h"
#include "cli/cli.h"
#include "music/music.h"
#include "third_party/cJSON/cJSON.h"
#include "aliot/aiot.h"
#include "ble.h"
#include <cstdio>
#include <cstring>
#include <stdarg.h>
#include <sstream>
#include <string>
#include <vector>

/* -------------------------------------------------------------------------
 * 日志
 * ------------------------------------------------------------------------- */
static mlog       *s_product_log      = NULL;
static mlog       *s_framework_log    = NULL;
static const char *k_default_log_name = "snack";
#define MQTT_LOG_CHUNK 900U /**< sink 为 1KB 栈缓冲；扣 [file:line] 与 snack_cloud 前缀后约 900 */

static void ensure_log_ready(const char *name)
{
    const char *resolved = name;

    if ((resolved == NULL) || (resolved[0] == '\0')) {
        resolved = k_default_log_name;
    }

    if (s_product_log == NULL) {
        s_product_log = new mlog(resolved);
    }
    if (s_framework_log == NULL) {
        s_framework_log = new mlog(SW_LOG_COMPONENT_BASE);
    }
}

static mlog *log_for_component(const char *component)
{
    ensure_log_ready(NULL);
    if ((component != NULL) && (std::strcmp(component, SW_LOG_COMPONENT_BASE) == 0)) {
        return s_framework_log;
    }
    return s_product_log;
}

static void snack_log_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    char buf[1024] = {0};
    mlog *logger    = log_for_component(component);

    vsnprintf(buf, sizeof(buf), fmt, ap);
    switch (level)
    {
        case SW_LOG_ERROR: logger->e(buf); break;
        case SW_LOG_WARN:  logger->w(buf); break;
        case SW_LOG_INFO:  logger->i(buf); break;
        case SW_LOG_DEBUG: logger->d(buf); break;
        default:           logger->i(buf); break;
    }
}

void snack_log_sink_register(const char *name)
{
    ensure_log_ready(name);
    sw_log_register_sink(snack_log_sink);
}

void set_log_level(int type)
{
    ensure_log_ready(NULL);
    s_product_log->levelSet((MLOG_type)type);
    s_product_log->setLogClearDays(0);
    s_framework_log->levelSet((MLOG_type)type);
    s_framework_log->setLogClearDays(0);
}

/* -------------------------------------------------------------------------
 * 阿里云 MQTT
 * ------------------------------------------------------------------------- */
#define SNACK_MQTT_CRED_MAX 64 /**< 与云适配器凭证缓冲对齐 */

static aiot               *s_mqtt_client  = NULL;
static mqtt_recv_handler_t s_mqtt_callback = NULL;
static char                s_product_key[SNACK_MQTT_CRED_MAX];
static char                s_device_name[SNACK_MQTT_CRED_MAX];
static char                s_device_secret[SNACK_MQTT_CRED_MAX];
static char                s_iot_instance[SNACK_MQTT_CRED_MAX];

static const char *mqtt_topic_or_empty(const char *topic)
{
    return (topic != NULL) ? topic : "";
}

/**
 * @brief  topic 与 JSON 同一行打印；超长时后续行只续正文，避免撑破 1KB sink
 */
static void log_mqtt_payload(const char *dir, const char *topic, const char *json)
{
    char   chunk[MQTT_LOG_CHUNK + 1U];
    size_t len;
    size_t off;
    size_t first_cap;
    size_t extra;

    if (json == NULL) {
        json = "";
    }
    topic     = mqtt_topic_or_empty(topic);
    len       = std::strlen(json);
    extra     = std::strlen(topic) + 32U; /* 相对续行多出的 "topic=... len=..." */
    first_cap = MQTT_LOG_CHUNK;
    if (extra < first_cap) {
        first_cap -= extra;
    } else {
        first_cap = 1U;
    }

    off = (len < first_cap) ? len : first_cap;
    (void)std::memcpy(chunk, json, off);
    chunk[off] = '\0';
    LOG_DEBUG("snack_cloud: %s topic=%s len=%u %s", dir, topic, (unsigned)len, chunk);

    while (off < len) {
        size_t n = len - off;

        if (n > MQTT_LOG_CHUNK) {
            n = MQTT_LOG_CHUNK;
        }
        (void)std::memcpy(chunk, json + off, n);
        chunk[n] = '\0';
        LOG_DEBUG("snack_cloud: %s +%u %s", dir, (unsigned)off, chunk);
        off += n;
    }
}

/* 阿里云平台下行消息格式：{"params": {...}} — 取 params 字段传给回调 */
static void client_deal(char *topic, char *msg, int msg_len)
{
    cJSON *root;
    cJSON *param;

    (void)msg_len;

    root = cJSON_Parse(msg);
    if (root == NULL) {
        LOG_WARN("snack_cloud: down invalid json topic=%s", mqtt_topic_or_empty(topic));
        return;
    }

    param = cJSON_GetObjectItem(root, "params");
    if ((s_mqtt_callback != NULL) && (param != NULL)) {
        char *str = cJSON_PrintUnformatted(param);

        if (str != NULL) {
            log_mqtt_payload("down", topic, str);
            s_mqtt_callback(str);
            free(str);
        }
    } else {
        LOG_WARN("snack_cloud: down missing params topic=%s", mqtt_topic_or_empty(topic));
    }

    cJSON_Delete(root);
}

/**
 * @brief  销毁旧客户端后按已保存凭证重建并连接
 * @return 0 已在线；非 0 失败
 */
static int mqtt_create_and_connect(void)
{
    if (s_mqtt_client != NULL) {
        delete s_mqtt_client;
        s_mqtt_client = NULL;
    }

    s_mqtt_client = new aiot(s_product_key, s_device_name, s_device_secret, s_iot_instance);
    if (s_mqtt_client == NULL) {
        return -2;
    }

    s_mqtt_client->connect(client_deal);
    s_mqtt_client->heartbeatSet(15);
    if (!s_mqtt_client->online) {
        return -3;
    }
    return 0;
}

int aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret, char *iot_instance)
{
    if (product_key == NULL || device_name == NULL || device_secret == NULL) {
        return -1;
    }

    (void)std::snprintf(s_product_key, sizeof(s_product_key), "%s", product_key);
    (void)std::snprintf(s_device_name, sizeof(s_device_name), "%s", device_name);
    (void)std::snprintf(s_device_secret, sizeof(s_device_secret), "%s", device_secret);
    if ((iot_instance != NULL) && (iot_instance[0] != '\0')) {
        (void)std::snprintf(s_iot_instance, sizeof(s_iot_instance), "%s", iot_instance);
    } else {
        s_iot_instance[0] = '\0';
    }

    return mqtt_create_and_connect();
}

int mqtt_connect(void)
{
    if (s_mqtt_client != NULL && s_mqtt_client->online) {
        return 0;
    }
    if ((s_product_key[0] == '\0') || (s_device_name[0] == '\0') || (s_device_secret[0] == '\0')) {
        return -1;
    }
    return mqtt_create_and_connect();
}

int mqtt_is_online(void)
{
    if (s_mqtt_client != NULL && s_mqtt_client->online) {
        return 1;
    }
    return 0;
}

/** 属性上报使用 QoS 1，避免增量在 QoS 0 下丢失 */
static const int SNACK_MQTT_PUBLISH_QOS = 1;

int net_mqtt_send(char *topic, char *msg)
{
    int ret;

    if (s_mqtt_client == NULL || topic == NULL || msg == NULL) {
        return -1;
    }
    /* 必须走 publish(topic, string, qos)。
     * publish(topic, const char*, ...) 限长 1K，且把 payload 当格式化串；
     * 全量属性 JSON 超过 1K 会被截断，阿里云报 payload must be json format。
     * QoS 1 保证变更增量至少一次投递。 */
    ret = s_mqtt_client->publish(topic, std::string(msg), SNACK_MQTT_PUBLISH_QOS);
    if (ret != 0) {
        LOG_WARN("snack_cloud: up failed topic=%s", topic);
    }
    log_mqtt_payload("up", topic, msg);
    return ret;
}

void mqtt_recv_handler_set(mqtt_recv_handler_t cb)
{
    s_mqtt_callback = cb;
}

/* -------------------------------------------------------------------------
 * 音乐播放
 * ------------------------------------------------------------------------- */
static music s_player;

void player_play(uint8_t item)
{
    char filePath[64] = {0};
    snprintf(filePath, sizeof(filePath), "/home/neardi/Music/00%d.mp3", item);
    s_player.play(filePath, 60);
}

/* -------------------------------------------------------------------------
 * BLE
 * ------------------------------------------------------------------------- */
static int (*s_osal_debug_cb)(char *fun, char *param_1, char *param_2) = NULL;

static bool ble_cmd_process(std::vector<std::string> &tokens)
{
    if (tokens.size() < 2) {
        return false;
    }
    if (tokens[0] == "osal" && s_osal_debug_cb != NULL) {
        const char *p1 = tokens.size() > 1 ? tokens[1].c_str() : "";
        const char *p2 = tokens.size() > 2 ? tokens[2].c_str() : "";
        const char *p3 = tokens.size() > 3 ? tokens[3].c_str() : "";
        s_osal_debug_cb((char *)p1, (char *)p2, (char *)p3);
    }
    return true;
}

void ble_init(void)
{
    BLE::ble.init();

    BLE::ble.set_data_char_recv_cb([&](std::string &msg, int mtu) {
        (void)mtu;
        std::istringstream iss(msg);
        std::string line;
        while (std::getline(iss, line, '\n')) {
            std::vector<std::string> tokens;
            std::istringstream line_iss(line);
            std::string token;
            LOG_INFO("ble cmd: %s", line.c_str());
            while (line_iss >> token) {
                tokens.push_back(token);
            }
            ble_cmd_process(tokens);
        }
    });

    BLE::ble.set_debug_char_recv_cb([&](std::string &msg, int mtu) {
        (void)mtu;
        LOG_INFO("ble debug recv: %s", msg.c_str());
    });
}

void ble_deinit(void)
{
    BLE::ble.deinit();
}

void ble_application_channel_send(char *data)
{
    BLE::ble.send_to_data_char(std::string(data));
}

void ble_debug_channel_send(char *data)
{
    BLE::ble.send_to_debug_char(std::string(data));
}

void osal_debug_callback_regist(int (*callback)(char *fun, char *param_1, char *param_2))
{
    s_osal_debug_cb = callback;
}

/* -------------------------------------------------------------------------
 * CLI
 * ------------------------------------------------------------------------- */
void cli_adapter_init(void)
{
    cli::init();
}

void cli_adapter_add(int (*cmd_fn)(void))
{
    cli::add(cmd_fn);
}

char *cli_adapter_get(int idx)
{
    return cli::get(idx);
}
