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
/* SDK 头文件把 ota_recv_cb 标为 private，但 libsnack 仍导出该符号；挂钩里须转调 Snack 原下载逻辑。 */
#define private public
#include "aliot/aiot.h"
#undef private
#include "aiot/aiot_ota_api.h"
#include "ble.h"
#include <cstdio>
#include <cstring>
#include <libgen.h>
#include <pthread.h>
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
static thread_local char s_snack_line[SW_LOG_LINE_MAX];

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
        s_framework_log = new mlog(SW_LOG_SOURCE_BASE);
    }
}

static mlog *log_for_source(const char *source)
{
    ensure_log_ready(NULL);
    if ((source != NULL) && (std::strcmp(source, SW_LOG_SOURCE_BASE) == 0)) {
        return s_framework_log;
    }
    return s_product_log;
}

static void snack_log_sink(sw_log_level_t level, const char *source, const char *fmt, va_list ap)
{
    mlog *logger = log_for_source(source);
    char  occurred[24];
    int   head;
    int   body;

    /* mlog 只能盖写出时刻；把调用点发生时刻嵌进正文，分析以正文时间为准 */
    (void)sw_log_format_occurred_at(NULL, occurred, sizeof(occurred), false);
    head = std::snprintf(s_snack_line, sizeof(s_snack_line), "[%s] ", occurred);
    if ((head < 0) || ((size_t)head >= sizeof(s_snack_line))) {
        return;
    }
    body = vsnprintf(&s_snack_line[head], sizeof(s_snack_line) - (size_t)head, fmt, ap);
    if (body < 0) {
        return;
    }
    (void)sw_log_clip_line(s_snack_line, sizeof(s_snack_line), head + body);
    switch (level)
    {
        case SW_LOG_ERROR: logger->e(s_snack_line); break;
        case SW_LOG_WARN:  logger->w(s_snack_line); break;
        case SW_LOG_INFO:  logger->i(s_snack_line); break;
        case SW_LOG_DEBUG: logger->d(s_snack_line); break;
        default:           logger->i(s_snack_line); break;
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
static int                 s_heartbeat_s = 0;

static const char *mqtt_topic_or_empty(const char *topic)
{
    return (topic != NULL) ? topic : "";
}

/**
 * @brief  topic 与 JSON 同一条日志打印
 */
static void log_mqtt_payload(const char *dir, const char *topic, const char *json)
{
    if (json == NULL) {
        json = "";
    }
    LOG_DEBUG("snack_cloud: %s topic=%s len=%u %s",
              dir,
              mqtt_topic_or_empty(topic),
              (unsigned)std::strlen(json),
              json);
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
    s_mqtt_client->heartbeatSet(s_heartbeat_s);
    /* 重建客户端会丢掉 OTA 订阅，须由调用方再报；此处不自动 ota_init */
    if (!s_mqtt_client->online) {
        return -3;
    }
    return 0;
}

int aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret, char *iot_instance, int heartbeat_s)
{
    if (product_key == NULL || device_name == NULL || device_secret == NULL || heartbeat_s <= 0) {
        return -1;
    }
    s_heartbeat_s = heartbeat_s;

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

/** `ota_init` 压缩包类型，取值与 SDK 一致 */
#define SNACK_OTA_INIT_TYPE_PACKAGE 1
/** 任务摘要 hex 缓冲（SHA-256 64 位 + NUL） */
#define SNACK_OTA_DIGEST_HEX_MAX 65U
/** sidecar 路径 = 原路径 + ".sha256" */
#define SNACK_OTA_SIDECAR_PATH_MAX 512U

/** 云端 OTA 任务使用的摘要算法 */
typedef enum {
    SNACK_OTA_DIGEST_NONE   = 0, /**< 尚无任务 */
    SNACK_OTA_DIGEST_MD5    = 1, /**< MD5 */
    SNACK_OTA_DIGEST_SHA256 = 2, /**< SHA-256 */
} snack_ota_digest_method_t;

static pthread_mutex_t           s_ota_digest_mux = PTHREAD_MUTEX_INITIALIZER;
static snack_ota_digest_method_t s_ota_digest_method = SNACK_OTA_DIGEST_NONE;
static char                      s_ota_digest_hex[SNACK_OTA_DIGEST_HEX_MAX];

/**
 * @brief  清空已缓存的任务摘要
 */
static void snack_ota_digest_clear_locked(void)
{
    s_ota_digest_method = SNACK_OTA_DIGEST_NONE;
    s_ota_digest_hex[0] = '\0';
}

/**
 * @brief  清空任务摘要（上报前调用，避免套用上一任务）
 */
static void snack_ota_digest_clear(void)
{
    (void)pthread_mutex_lock(&s_ota_digest_mux);
    snack_ota_digest_clear_locked();
    (void)pthread_mutex_unlock(&s_ota_digest_mux);
}

/**
 * @brief  从任务描述拷走算法与摘要
 * @param  desc 阿里云任务；空或字段不全则忽略
 */
static void snack_ota_digest_store(const aiot_download_task_desc_t *desc)
{
    snack_ota_digest_method_t method;
    size_t                    len;

    if ((desc == NULL) || (desc->expect_digest == NULL) || (desc->expect_digest[0] == '\0')) {
        return;
    }
    if (desc->digest_method == AIOT_OTA_DIGEST_MD5) {
        method = SNACK_OTA_DIGEST_MD5;
    } else if (desc->digest_method == AIOT_OTA_DIGEST_SHA256) {
        method = SNACK_OTA_DIGEST_SHA256;
    } else {
        return;
    }
    len = std::strlen(desc->expect_digest);
    if ((len == 0U) || (len >= sizeof(s_ota_digest_hex))) {
        return;
    }
    (void)pthread_mutex_lock(&s_ota_digest_mux);
    s_ota_digest_method = method;
    (void)std::memcpy(s_ota_digest_hex, desc->expect_digest, len + 1U);
    (void)pthread_mutex_unlock(&s_ota_digest_mux);
}

/**
 * @brief  先拷任务摘要，再交给 Snack 原回调继续下载
 */
static void snack_ota_recv_wrapper(void *handle, const aiot_ota_recv_t *const msg, void *userdata)
{
    if ((msg != NULL) && (msg->task_desc != NULL)) {
        snack_ota_digest_store(msg->task_desc);
    }
    aiot::ota_recv_cb(handle, const_cast<void *>(static_cast<const void *>(msg)), userdata);
}

/**
 * @brief  把包装回调挂到本次 `ota_init` 创建的句柄上
 */
static void snack_ota_hook_recv(void)
{
    int32_t ret;

    if ((s_mqtt_client == NULL) || (s_mqtt_client->ota == NULL)) {
        return;
    }
    ret = aiot_ota_setopt(s_mqtt_client->ota, AIOT_OTAOPT_RECV_HANDLER, (void *)snack_ota_recv_wrapper);
    if (ret != 0) {
        LOG_WARN("snack_cloud: ota recv hook failed ret=%d", (int)ret);
    }
}

int snack_ota_report(const char *version)
{
    int ret;

    if ((version == NULL) || (version[0] == '\0')) {
        return -1;
    }
    if ((s_mqtt_client == NULL) || !s_mqtt_client->online) {
        return -2;
    }
    snack_ota_digest_clear();
    ret = s_mqtt_client->ota_init(version, SNACK_OTA_INIT_TYPE_PACKAGE);
    if (ret != 0) {
        LOG_WARN("snack_cloud: ota_init failed ret=%d version=%s", ret, version);
        return ret;
    }
    snack_ota_hook_recv();
    LOG_INFO("snack_cloud: ota_init version=%s", version);
    return 0;
}

bool snack_ota_busy(void)
{
    return aiot::ota_new;
}

/**
 * @brief  拷出最近任务的摘要
 * @param  digest 至少 SNACK_OTA_DIGEST_HEX_MAX 字节
 * @return 尚无任务时为 SNACK_OTA_DIGEST_NONE
 */
static snack_ota_digest_method_t snack_ota_digest_copy(char *digest)
{
    snack_ota_digest_method_t method;

    (void)pthread_mutex_lock(&s_ota_digest_mux);
    method = s_ota_digest_method;
    (void)std::snprintf(digest, SNACK_OTA_DIGEST_HEX_MAX, "%s", s_ota_digest_hex);
    (void)pthread_mutex_unlock(&s_ota_digest_mux);
    return method;
}

int snack_ota_write_job_sidecar(const char *bundle_path)
{
    snack_ota_digest_method_t method;
    char                      digest[SNACK_OTA_DIGEST_HEX_MAX];
    char                      sidecar[SNACK_OTA_SIDECAR_PATH_MAX];
    char                      name_buf[SNACK_OTA_SIDECAR_PATH_MAX];
    const char               *suffix;
    const char               *base;
    FILE                     *fp;
    int                      n;

    if ((bundle_path == NULL) || (bundle_path[0] == '\0')) {
        return -1;
    }
    method = snack_ota_digest_copy(digest);
    if (method == SNACK_OTA_DIGEST_MD5) {
        suffix = ".md5";
    } else if (method == SNACK_OTA_DIGEST_SHA256) {
        suffix = ".sha256";
    } else {
        return -1;
    }
    n = std::snprintf(sidecar, sizeof(sidecar), "%s%s", bundle_path, suffix);
    if ((n < 0) || ((size_t)n >= sizeof(sidecar))) {
        return -1;
    }
    (void)std::snprintf(name_buf, sizeof(name_buf), "%s", bundle_path);
    base = basename(name_buf);
    if ((base == NULL) || (base[0] == '\0')) {
        return -1;
    }
    fp = std::fopen(sidecar, "w");
    if (fp == NULL) {
        LOG_WARN("snack_cloud: cannot write ota sidecar path=%s", sidecar);
        return -1;
    }
    (void)std::fprintf(fp, "%s  %s\n", digest, base);
    (void)std::fclose(fp);
    return 0;
}

/* -------------------------------------------------------------------------
 * 音乐播放
 * ------------------------------------------------------------------------- */
static music s_player;

#define PLAYER_PATH_MAX 256U

void player_play(const char *file_path, int volume)
{
    char path[PLAYER_PATH_MAX] = {0};

    if (file_path == NULL) {
        return;
    }
    /* 沿用 SDK 原有的可写缓冲区传参形式 */
    snprintf(path, sizeof(path), "%s", file_path);
    s_player.play(path, volume);
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
