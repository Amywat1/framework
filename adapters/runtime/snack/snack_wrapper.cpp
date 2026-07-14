/**
 * @file    snack_wrapper.cpp
 * @brief   snack SDK 封装实现（日志、阿里云 MQTT、音乐、BLE、CLI）
 * @author  HUWANGWEI
 * @date    2026-04-08
 */

#include "framework/adapters/runtime/snack/snack_log.h"
#include "framework/adapters/runtime/snack/snack_mqtt.h"
#include "framework/adapters/runtime/snack/snack_player.h"
#include "framework/adapters/runtime/snack/snack_ble.h"
#include "framework/adapters/runtime/snack/snack_cli.h"
#include "framework/common/log.h"
#include "log/mlog.h"
#include "cli/cli.h"
#include "music/music.h"
#include "third_party/cJSON/cJSON.h"
#include "aliot/aiot.h"
#include "ble.h"
#include <stdarg.h>
#include <sstream>
#include <string>
#include <vector>

/* -------------------------------------------------------------------------
 * 日志
 * ------------------------------------------------------------------------- */
static mlog       *s_log             = NULL;
static const char *k_default_log_name = "snack";

static void ensure_log_ready(const char *name)
{
    const char *resolved = name;

    if ((resolved == NULL) || (resolved[0] == '\0')) {
        resolved = k_default_log_name;
    }

    if (s_log == NULL) {
        s_log = new mlog(resolved);
    }
}

static void snack_log_sink(sw_log_level_t level, const char *fmt, va_list ap)
{
    char buf[1024] = {0};

    ensure_log_ready(NULL);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    switch (level)
    {
        case SW_LOG_ERROR: s_log->e(buf); break;
        case SW_LOG_WARN:  s_log->w(buf); break;
        case SW_LOG_INFO:  s_log->i(buf); break;
        case SW_LOG_DEBUG: s_log->d(buf); break;
        default:           s_log->i(buf); break;
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
    s_log->levelSet((MLOG_type)type);
    s_log->setLogClearDays(0);
}

/* -------------------------------------------------------------------------
 * 阿里云 MQTT
 * ------------------------------------------------------------------------- */
static aiot               *s_mqtt_client  = NULL;
static mqtt_recv_handler_t s_mqtt_callback = NULL;

/* 阿里云平台下行消息格式：{"params": {...}} — 取 params 字段传给回调 */
static void client_deal(char *topic, char *msg, int msg_len)
{
    (void)topic;
    (void)msg_len;

    cJSON *root  = cJSON_Parse(msg);
    cJSON *param = cJSON_GetObjectItem(root, "params");

    if (s_mqtt_callback != NULL && param != NULL) {
        char *str = cJSON_PrintUnformatted(param);
        s_log->i("mqtt recv: %s", str);
        s_mqtt_callback(str);
        free(str);
    }

    cJSON_Delete(root);
}

int aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret)
{
    if (product_key == NULL || device_name == NULL || device_secret == NULL) {
        return -1;
    }

    s_mqtt_client = new aiot(product_key, device_name, device_secret, "");
    if (s_mqtt_client == NULL) {
        return -2;
    }

    s_mqtt_client->connect(client_deal);
    s_mqtt_client->heartbeatSet(15);

    return 0;
}

int mqtt_is_online(void)
{
    if (s_mqtt_client != NULL && s_mqtt_client->online) {
        return 1;
    }
    return 0;
}

int net_mqtt_send(char *topic, char *msg)
{
    if (s_mqtt_client == NULL || topic == NULL || msg == NULL) {
        return -1;
    }
    return s_mqtt_client->publish(topic, msg);
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
