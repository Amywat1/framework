#include "adapters/providers/snack/snack_sdk.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int                 init_result;
    int                 connect_result;
    int                 online;
    int                 send_result;
    int                 ota_report_result;
    bool                ota_busy;
    unsigned            connect_calls;
    unsigned            ota_report_calls;
    int                       ota_type;
    snack_ota_digest_method_t ota_digest_method;
    uint32_t                  ota_digest_size;
    char                      product_key[64];
    char                      device_name[64];
    char                      device_secret[64];
    char                      iot_instance[64];
    char                      last_topic[128];
    char                      last_payload[256];
    char                      ota_version[64];
    char                      ota_digest_hex[SNACK_OTA_DIGEST_HEX_MAX];
    mqtt_recv_handler_t       recv_cb;
} snack_mqtt_fake_t;

static snack_mqtt_fake_t s_fake;

void snack_mqtt_fake_reset(void)
{
    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.init_result = 0;
    s_fake.send_result = 0;
}

void snack_mqtt_fake_set_online(int online)
{
    s_fake.online = online;
}

void snack_mqtt_fake_set_init_result(int result)
{
    s_fake.init_result = result;
}

void snack_mqtt_fake_set_connect_result(int result)
{
    s_fake.connect_result = result;
}

unsigned snack_mqtt_fake_connect_calls(void)
{
    return s_fake.connect_calls;
}

void snack_mqtt_fake_set_send_result(int result)
{
    s_fake.send_result = result;
}

const char *snack_mqtt_fake_product_key(void)
{
    return s_fake.product_key;
}

const char *snack_mqtt_fake_device_name(void)
{
    return s_fake.device_name;
}

const char *snack_mqtt_fake_device_secret(void)
{
    return s_fake.device_secret;
}

const char *snack_mqtt_fake_last_topic(void)
{
    return s_fake.last_topic;
}

const char *snack_mqtt_fake_last_payload(void)
{
    return s_fake.last_payload;
}

mqtt_recv_handler_t snack_mqtt_fake_recv_handler(void)
{
    return s_fake.recv_cb;
}

int aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret, char *iot_instance, int heartbeat_s)
{
    (void)heartbeat_s;
    snprintf(s_fake.product_key, sizeof(s_fake.product_key), "%s", product_key != NULL ? product_key : "");
    snprintf(s_fake.device_name, sizeof(s_fake.device_name), "%s", device_name != NULL ? device_name : "");
    snprintf(s_fake.device_secret, sizeof(s_fake.device_secret), "%s", device_secret != NULL ? device_secret : "");
    snprintf(s_fake.iot_instance, sizeof(s_fake.iot_instance), "%s", iot_instance != NULL ? iot_instance : "");
    if (s_fake.init_result == 0) {
        s_fake.online = 1;
    } else {
        s_fake.online = 0;
    }
    return s_fake.init_result;
}

int mqtt_connect(void)
{
    s_fake.connect_calls++;
    if (s_fake.online != 0) {
        return 0;
    }
    if (s_fake.connect_result == 0) {
        s_fake.online = 1;
        return 0;
    }
    return s_fake.connect_result;
}

int mqtt_is_online(void)
{
    return s_fake.online;
}

int net_mqtt_send(char *topic, char *msg)
{
    snprintf(s_fake.last_topic, sizeof(s_fake.last_topic), "%s", topic != NULL ? topic : "");
    snprintf(s_fake.last_payload, sizeof(s_fake.last_payload), "%s", msg != NULL ? msg : "");
    return s_fake.send_result;
}

void mqtt_recv_handler_set(mqtt_recv_handler_t cb)
{
    s_fake.recv_cb = cb;
}

int snack_ota_report(const char *version, int type)
{
    if ((version == NULL) || (version[0] == '\0')) {
        return -1;
    }
    s_fake.ota_type = type;
    s_fake.ota_report_calls++;
    snprintf(s_fake.ota_version, sizeof(s_fake.ota_version), "%s", version);
    s_fake.ota_digest_method = SNACK_OTA_DIGEST_NONE;
    s_fake.ota_digest_hex[0] = '\0';
    s_fake.ota_digest_size   = 0U;
    return s_fake.ota_report_result;
}

int snack_ota_job_digest(snack_ota_digest_method_t *method, char *digest, size_t digest_len,
                         uint32_t *size_bytes)
{
    if ((method == NULL) || (digest == NULL) || (digest_len < SNACK_OTA_DIGEST_HEX_MAX)) {
        return -1;
    }
    if (s_fake.ota_digest_method == SNACK_OTA_DIGEST_NONE) {
        return -1;
    }
    *method = s_fake.ota_digest_method;
    snprintf(digest, digest_len, "%s", s_fake.ota_digest_hex);
    if (size_bytes != NULL) {
        *size_bytes = s_fake.ota_digest_size;
    }
    return 0;
}

int snack_ota_write_job_sidecar(const char *bundle_path)
{
    snack_ota_digest_method_t method;
    char                      digest[SNACK_OTA_DIGEST_HEX_MAX];
    char                      sidecar[512];
    const char               *suffix;
    const char               *base;
    FILE                     *fp;
    int                       n;

    if ((bundle_path == NULL) || (bundle_path[0] == '\0')) {
        return -1;
    }
    if (snack_ota_job_digest(&method, digest, sizeof(digest), NULL) != 0) {
        return -1;
    }
    if (method == SNACK_OTA_DIGEST_MD5) {
        suffix = ".md5";
    } else if (method == SNACK_OTA_DIGEST_SHA256) {
        suffix = ".sha256";
    } else {
        return -1;
    }
    n = snprintf(sidecar, sizeof(sidecar), "%s%s", bundle_path, suffix);
    if ((n < 0) || ((size_t)n >= sizeof(sidecar))) {
        return -1;
    }
    base = strrchr(bundle_path, '/');
    base = (base != NULL) ? (base + 1) : bundle_path;
    if (base[0] == '\0') {
        return -1;
    }
    fp = fopen(sidecar, "w");
    if (fp == NULL) {
        return -1;
    }
    (void)fprintf(fp, "%s  %s\n", digest, base);
    (void)fclose(fp);
    return 0;
}

bool snack_ota_busy(void)
{
    return s_fake.ota_busy;
}

void snack_mqtt_fake_set_ota_result(int result)
{
    s_fake.ota_report_result = result;
}

void snack_mqtt_fake_set_ota_busy(bool busy)
{
    s_fake.ota_busy = busy;
}

void snack_mqtt_fake_set_ota_job_digest(snack_ota_digest_method_t method, const char *digest,
                                        uint32_t size_bytes)
{
    s_fake.ota_digest_method = method;
    s_fake.ota_digest_size   = size_bytes;
    if (digest != NULL) {
        snprintf(s_fake.ota_digest_hex, sizeof(s_fake.ota_digest_hex), "%s", digest);
    } else {
        s_fake.ota_digest_hex[0] = '\0';
        s_fake.ota_digest_method = SNACK_OTA_DIGEST_NONE;
    }
}

unsigned snack_mqtt_fake_ota_report_calls(void)
{
    return s_fake.ota_report_calls;
}

const char *snack_mqtt_fake_ota_version(void)
{
    return s_fake.ota_version;
}

int snack_mqtt_fake_ota_type(void)
{
    return s_fake.ota_type;
}
