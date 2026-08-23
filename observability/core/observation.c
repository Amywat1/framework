/**
 * @file    observation.c
 * @brief   通用可观测记录非阻塞发布实现
 */

#include "observability/core/observation.h"

#include "common/time_util.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    observation_record_t records[OBSERVATION_NORMAL_QUEUE_CAPACITY];
    uint32_t             head;
    uint32_t             tail;
    uint32_t             count;
} normal_queue_t;

typedef struct {
    observation_record_t records[OBSERVATION_CRITICAL_QUEUE_CAPACITY];
    uint32_t             head;
    uint32_t             tail;
    uint32_t             count;
} critical_queue_t;

static normal_queue_t        s_normal_queue;
static critical_queue_t      s_critical_queue;
static observation_stats_t   s_stats;
static observation_context_t s_context;
static observation_record_t  s_latest_record;
static uint64_t              s_boot_id;
static uint64_t              s_next_sequence;
static uint64_t              s_log_window_started_ms;
static uint32_t              s_log_window_count;
static bool                  s_initialized;
static bool                  s_export_available;
static bool                  s_latest_pending;
static pthread_mutex_t       s_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t wall_time_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0U;
    }
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static bool spec_is_valid(const observation_record_spec_t *spec)
{
    if ((spec == NULL) || (spec->source == NULL)) {
        return false;
    }
    if ((spec->kind < OBSERVATION_RECORD_EVENT) || (spec->kind > OBSERVATION_RECORD_INCIDENT)) {
        return false;
    }
    if ((spec->severity < OBSERVATION_SEVERITY_DEBUG) || (spec->severity > OBSERVATION_SEVERITY_CRITICAL)) {
        return false;
    }
    if ((spec->payload_format < OBSERVATION_PAYLOAD_NONE) || (spec->payload_format > OBSERVATION_PAYLOAD_BINARY)) {
        return false;
    }
    if ((spec->delivery < OBSERVATION_DELIVERY_BEST_EFFORT) || (spec->delivery > OBSERVATION_DELIVERY_LATEST)) {
        return false;
    }
    if (spec->payload_size > OBSERVATION_PAYLOAD_MAX) {
        return false;
    }
    return (spec->payload_size == 0U) || (spec->payload != NULL);
}

static void fill_record(observation_record_t *record, const observation_record_spec_t *spec)
{
    size_t source_len = strnlen(spec->source, OBSERVATION_SOURCE_MAX - 1U);

    memset(record, 0, sizeof(*record));
    record->schema_version = OBSERVATION_SCHEMA_VERSION;
    record->kind           = spec->kind;
    record->severity       = spec->severity;
    record->payload_format = spec->payload_format;
    record->delivery       = spec->delivery;
    record->event_code     = spec->event_code;
    record->boot_id        = s_boot_id;
    record->sequence       = s_next_sequence++;
    record->wall_time_ms   = wall_time_ms();
    record->monotonic_ms   = time_util_get_ms();
    record->context        = (spec->context != NULL) ? *spec->context : s_context;
    memcpy(record->source, spec->source, source_len);
    record->source[source_len] = '\0';
    record->payload_size       = (uint16_t)spec->payload_size;
    record->repeat_count       = 1U;
    if (spec->payload_size > 0U) {
        memcpy(record->payload, spec->payload, spec->payload_size);
    }
}

sw_err_t observation_init(uint64_t boot_id)
{
    if (boot_id == 0U) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    memset(&s_normal_queue, 0, sizeof(s_normal_queue));
    memset(&s_critical_queue, 0, sizeof(s_critical_queue));
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_context, 0, sizeof(s_context));
    memset(&s_latest_record, 0, sizeof(s_latest_record));
    s_boot_id               = boot_id;
    s_next_sequence         = 1U;
    s_log_window_started_ms = 0U;
    s_log_window_count      = 0U;
    s_export_available      = true;
    s_latest_pending        = false;
    s_initialized           = true;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

bool observation_is_ready(void)
{
    bool ready;

    pthread_mutex_lock(&s_mutex);
    ready = s_initialized;
    pthread_mutex_unlock(&s_mutex);
    return ready;
}

void observation_context_set(const observation_context_t *context)
{
    pthread_mutex_lock(&s_mutex);
    if (context == NULL) {
        memset(&s_context, 0, sizeof(s_context));
    } else {
        s_context = *context;
    }
    pthread_mutex_unlock(&s_mutex);
}

observation_context_t observation_context_get(void)
{
    observation_context_t out;

    pthread_mutex_lock(&s_mutex);
    out = s_context;
    pthread_mutex_unlock(&s_mutex);
    return out;
}

sw_err_t observation_publish(const observation_record_spec_t *spec)
{
    observation_record_t  *record;
    observation_delivery_t delivery;
    uint64_t               now_ms;

    if (!spec_is_valid(spec)) {
        return SW_ERR_PARAM;
    }
    if (pthread_mutex_trylock(&s_mutex) != 0) {
        __sync_fetch_and_add(&s_stats.dropped_busy_count, 1U);
        return SW_ERR_OVERFLOW;
    }
    if (!s_initialized) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }

    delivery = spec->delivery;
    if ((delivery == OBSERVATION_DELIVERY_BEST_EFFORT) && (spec->severity >= OBSERVATION_SEVERITY_ERROR)) {
        delivery = OBSERVATION_DELIVERY_RELIABLE;
    }
    if ((delivery == OBSERVATION_DELIVERY_BEST_EFFORT) && !s_export_available) {
        s_stats.dropped_offline_count++;
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_OVERFLOW;
    }
    now_ms = time_util_get_ms();
    if ((delivery == OBSERVATION_DELIVERY_BEST_EFFORT) && (spec->kind == OBSERVATION_RECORD_LOG)
        && (spec->severity <= OBSERVATION_SEVERITY_INFO)) {
        if ((s_log_window_started_ms == 0U) || (time_elapsed_ms(s_log_window_started_ms, now_ms) >= 1000U)) {
            s_log_window_started_ms = now_ms;
            s_log_window_count      = 0U;
        }
        if (s_log_window_count >= OBSERVATION_LOG_RATE_LIMIT_PER_SEC) {
            s_stats.dropped_rate_limited_count++;
            pthread_mutex_unlock(&s_mutex);
            return SW_ERR_OVERFLOW;
        }
        s_log_window_count++;
    }
    if (delivery == OBSERVATION_DELIVERY_LATEST) {
        if (s_latest_pending) {
            s_stats.latest_replaced_count++;
        }
        fill_record(&s_latest_record, spec);
        s_latest_record.delivery = delivery;
        s_latest_pending         = true;
        s_stats.latest_pending   = true;
    } else if (delivery == OBSERVATION_DELIVERY_RELIABLE) {
        if ((spec->kind == OBSERVATION_RECORD_LOG) && (s_critical_queue.count > 0U)) {
            uint32_t last_index = (s_critical_queue.tail + OBSERVATION_CRITICAL_QUEUE_CAPACITY - 1U)
                                  % OBSERVATION_CRITICAL_QUEUE_CAPACITY;
            observation_record_t *last       = &s_critical_queue.records[last_index];
            size_t                source_len = strnlen(spec->source, OBSERVATION_SOURCE_MAX);

            if ((last->kind == OBSERVATION_RECORD_LOG) && (last->event_code == spec->event_code)
                && (last->severity == spec->severity) && (last->payload_size == spec->payload_size)
                && (source_len < OBSERVATION_SOURCE_MAX) && (strcmp(last->source, spec->source) == 0)
                && ((spec->payload_size == 0U) || (memcmp(last->payload, spec->payload, spec->payload_size) == 0))) {
                if (last->repeat_count < UINT16_MAX) {
                    last->repeat_count++;
                }
                last->wall_time_ms = wall_time_ms();
                last->monotonic_ms = now_ms;
                s_stats.reliable_coalesced_count++;
                s_stats.published_count++;
                pthread_mutex_unlock(&s_mutex);
                return SW_OK;
            }
        }
        if (s_critical_queue.count >= OBSERVATION_CRITICAL_QUEUE_CAPACITY) {
            s_stats.dropped_critical_full_count++;
            pthread_mutex_unlock(&s_mutex);
            return SW_ERR_OVERFLOW;
        }
        record = &s_critical_queue.records[s_critical_queue.tail];
        fill_record(record, spec);
        record->delivery      = delivery;
        s_critical_queue.tail = (s_critical_queue.tail + 1U) % OBSERVATION_CRITICAL_QUEUE_CAPACITY;
        s_critical_queue.count++;
        s_stats.critical_queue_depth = s_critical_queue.count;
        if (s_critical_queue.count > s_stats.critical_queue_peak) {
            s_stats.critical_queue_peak = s_critical_queue.count;
        }
    } else {
        if (s_normal_queue.count >= OBSERVATION_NORMAL_QUEUE_CAPACITY) {
            s_stats.dropped_normal_full_count++;
            pthread_mutex_unlock(&s_mutex);
            return SW_ERR_OVERFLOW;
        }
        record = &s_normal_queue.records[s_normal_queue.tail];
        fill_record(record, spec);
        record->delivery    = delivery;
        s_normal_queue.tail = (s_normal_queue.tail + 1U) % OBSERVATION_NORMAL_QUEUE_CAPACITY;
        s_normal_queue.count++;
        s_stats.normal_queue_depth = s_normal_queue.count;
        if (s_normal_queue.count > s_stats.normal_queue_peak) {
            s_stats.normal_queue_peak = s_normal_queue.count;
        }
    }
    s_stats.published_count++;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

void observation_set_export_available(bool available)
{
    pthread_mutex_lock(&s_mutex);
    if (!available && s_export_available && (s_normal_queue.count > 0U)) {
        s_stats.discarded_on_disconnect_count += s_normal_queue.count;
        memset(&s_normal_queue, 0, sizeof(s_normal_queue));
        s_stats.normal_queue_depth = 0U;
    }
    s_export_available = available;
    pthread_mutex_unlock(&s_mutex);
}

sw_err_t observation_try_pop(observation_record_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }
    if (pthread_mutex_trylock(&s_mutex) != 0) {
        return SW_ERR_NOT_FOUND;
    }
    if (s_latest_pending) {
        *out                   = s_latest_record;
        s_latest_pending       = false;
        s_stats.latest_pending = false;
    } else if (s_critical_queue.count > 0U) {
        *out                  = s_critical_queue.records[s_critical_queue.head];
        s_critical_queue.head = (s_critical_queue.head + 1U) % OBSERVATION_CRITICAL_QUEUE_CAPACITY;
        s_critical_queue.count--;
        s_stats.critical_queue_depth = s_critical_queue.count;
    } else if (s_normal_queue.count > 0U) {
        *out                = s_normal_queue.records[s_normal_queue.head];
        s_normal_queue.head = (s_normal_queue.head + 1U) % OBSERVATION_NORMAL_QUEUE_CAPACITY;
        s_normal_queue.count--;
        s_stats.normal_queue_depth = s_normal_queue.count;
    } else {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_FOUND;
    }
    if ((out->repeat_count > 1U) && (out->kind == OBSERVATION_RECORD_LOG)
        && (out->payload_format == OBSERVATION_PAYLOAD_TEXT)) {
        char suffix[32];
        int  suffix_len = snprintf(suffix, sizeof(suffix), " [重复=%u]", (unsigned)out->repeat_count);

        if ((suffix_len > 0) && ((size_t)out->payload_size + (size_t)suffix_len <= OBSERVATION_PAYLOAD_MAX)) {
            memcpy(&out->payload[out->payload_size], suffix, (size_t)suffix_len);
            out->payload_size = (uint16_t)(out->payload_size + (uint16_t)suffix_len);
        }
    }
    s_stats.popped_count++;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t observation_get_stats(observation_stats_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }
    pthread_mutex_lock(&s_mutex);
    *out = s_stats;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}
