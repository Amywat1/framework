/**
 * @file    blackbox_recorder.c
 * @brief   通用故障黑匣子循环采样实现
 */

#include "observability/recorder/blackbox_recorder.h"

#include "common/time_util.h"

#include <pthread.h>
#include <string.h>

static blackbox_sample_t s_samples[BLACKBOX_SAMPLE_CAPACITY];
static blackbox_config_t s_config;
static uint32_t          s_pre_capacity;
static uint32_t          s_total_capacity;
static uint32_t          s_head;
static uint32_t          s_count;
static uint32_t          s_post_remaining;
static uint64_t          s_incident_id;
static uint32_t          s_trigger_code;
static uint64_t          s_trigger_monotonic_ms;
static uint64_t          s_dropped_busy_count;
static uint64_t          s_dropped_frozen_count;
static blackbox_state_t  s_state;
static bool              s_initialized;
static pthread_mutex_t   s_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t configured_total_samples(const blackbox_config_t *config)
{
    return (uint32_t)config->sample_rate_hz
           * ((uint32_t)config->pre_trigger_seconds + (uint32_t)config->post_trigger_seconds);
}

sw_err_t blackbox_recorder_init(const blackbox_config_t *config)
{
    if ((config == NULL) || (config->sample_rate_hz == 0U) || (config->pre_trigger_seconds == 0U)
        || (configured_total_samples(config) > BLACKBOX_SAMPLE_CAPACITY)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    memset(s_samples, 0, sizeof(s_samples));
    s_config               = *config;
    s_pre_capacity         = (uint32_t)config->sample_rate_hz * (uint32_t)config->pre_trigger_seconds;
    s_total_capacity       = configured_total_samples(config);
    s_head                 = 0U;
    s_count                = 0U;
    s_post_remaining       = 0U;
    s_incident_id          = 0U;
    s_trigger_code         = 0U;
    s_trigger_monotonic_ms = 0U;
    s_dropped_busy_count   = 0U;
    s_dropped_frozen_count = 0U;
    s_state                = BLACKBOX_STATE_ARMED;
    s_initialized          = true;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t blackbox_recorder_record(uint32_t sample_group, const void *payload, size_t payload_size)
{
    blackbox_sample_t *sample;
    uint32_t           active_capacity;
    uint32_t           write_index;

    if ((payload == NULL) || (payload_size == 0U) || (payload_size > BLACKBOX_SAMPLE_PAYLOAD_MAX)) {
        return SW_ERR_PARAM;
    }
    if (pthread_mutex_trylock(&s_mutex) != 0) {
        __sync_fetch_and_add(&s_dropped_busy_count, 1U);
        return SW_ERR_OVERFLOW;
    }
    if (!s_initialized) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (s_state == BLACKBOX_STATE_FROZEN) {
        s_dropped_frozen_count++;
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_OVERFLOW;
    }

    active_capacity = (s_state == BLACKBOX_STATE_ARMED) ? s_pre_capacity : s_total_capacity;
    if (s_count < active_capacity) {
        write_index = (s_head + s_count) % s_total_capacity;
        s_count++;
    } else {
        write_index = s_head;
        s_head      = (s_head + 1U) % s_total_capacity;
    }

    sample = &s_samples[write_index];
    memset(sample, 0, sizeof(*sample));
    sample->monotonic_ms = time_util_get_ms();
    sample->sample_group = sample_group;
    sample->payload_size = (uint16_t)payload_size;
    memcpy(sample->payload, payload, payload_size);

    if ((s_state == BLACKBOX_STATE_POST_TRIGGER) && (s_post_remaining > 0U)) {
        s_post_remaining--;
        if (s_post_remaining == 0U) {
            s_state = BLACKBOX_STATE_FROZEN;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t blackbox_recorder_trigger(uint64_t incident_id, uint32_t trigger_code)
{
    if (incident_id == 0U) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (!s_initialized) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (s_state != BLACKBOX_STATE_ARMED) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_BUSY;
    }

    s_incident_id          = incident_id;
    s_trigger_code         = trigger_code;
    s_trigger_monotonic_ms = time_util_get_ms();
    s_post_remaining       = (uint32_t)s_config.sample_rate_hz * (uint32_t)s_config.post_trigger_seconds;
    s_state                = (s_post_remaining == 0U) ? BLACKBOX_STATE_FROZEN : BLACKBOX_STATE_POST_TRIGGER;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t blackbox_recorder_get_info(blackbox_snapshot_info_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }
    pthread_mutex_lock(&s_mutex);
    out->incident_id          = s_incident_id;
    out->trigger_code         = s_trigger_code;
    out->trigger_monotonic_ms = s_trigger_monotonic_ms;
    out->sample_count         = s_count;
    out->state                = s_state;
    out->dropped_busy_count   = s_dropped_busy_count;
    out->dropped_frozen_count = s_dropped_frozen_count;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t blackbox_recorder_copy_sample(uint32_t index, blackbox_sample_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }
    pthread_mutex_lock(&s_mutex);
    if (s_state != BLACKBOX_STATE_FROZEN) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_BUSY;
    }
    if (index >= s_count) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_FOUND;
    }
    *out = s_samples[(s_head + index) % s_total_capacity];
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t blackbox_recorder_release(uint64_t incident_id)
{
    uint32_t retained_count;

    pthread_mutex_lock(&s_mutex);
    if (s_state != BLACKBOX_STATE_FROZEN) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_BUSY;
    }
    if ((incident_id == 0U) || (incident_id != s_incident_id)) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }

    retained_count = (s_count < s_pre_capacity) ? s_count : s_pre_capacity;
    if (s_count > retained_count) {
        s_head = (s_head + s_count - retained_count) % s_total_capacity;
    }
    s_count                = retained_count;
    s_incident_id          = 0U;
    s_trigger_code         = 0U;
    s_trigger_monotonic_ms = 0U;
    s_post_remaining       = 0U;
    s_state                = BLACKBOX_STATE_ARMED;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}
