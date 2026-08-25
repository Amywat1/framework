#ifndef TESTS_STUBS_SNACK_CLOUD_SNACK_MQTT_FAKE_H
#define TESTS_STUBS_SNACK_CLOUD_SNACK_MQTT_FAKE_H

#include "adapters/providers/snack/snack_sdk.h"

void                snack_mqtt_fake_reset(void);
void                snack_mqtt_fake_set_online(int online);
void                snack_mqtt_fake_set_init_result(int result);
void                snack_mqtt_fake_set_send_result(int result);
const char         *snack_mqtt_fake_product_key(void);
const char         *snack_mqtt_fake_device_name(void);
const char         *snack_mqtt_fake_device_secret(void);
const char         *snack_mqtt_fake_last_topic(void);
const char         *snack_mqtt_fake_last_payload(void);
mqtt_recv_handler_t snack_mqtt_fake_recv_handler(void);

#endif /* TESTS_STUBS_SNACK_CLOUD_SNACK_MQTT_FAKE_H */
