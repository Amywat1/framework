#include "adapters/runtime/snack/snack_sdk.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int                 init_result;
    int                 online;
    int                 send_result;
    char                product_key[64];
    char                device_name[64];
    char                device_secret[64];
    char                last_topic[128];
    char                last_payload[256];
    mqtt_recv_handler_t recv_cb;
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

int aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret)
{
    snprintf(s_fake.product_key, sizeof(s_fake.product_key), "%s", product_key != NULL ? product_key : "");
    snprintf(s_fake.device_name, sizeof(s_fake.device_name), "%s", device_name != NULL ? device_name : "");
    snprintf(s_fake.device_secret, sizeof(s_fake.device_secret), "%s", device_secret != NULL ? device_secret : "");
    if (s_fake.init_result == 0) {
        s_fake.online = 1;
    }
    return s_fake.init_result;
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
