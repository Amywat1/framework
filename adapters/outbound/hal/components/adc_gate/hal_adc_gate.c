/**
 * @file    hal_adc_gate.c
 * @brief   ADC 采样按需门控实现
 * @author  HUWANGWEI
 * @date    2026-07-27
 *
 * @note    共享状态由 s_lock 保护，acquire/release/is_needed 可在不同线程中并发调用。
 */

#include "adapters/outbound/hal/components/adc_gate/hal_adc_gate.h"

#include "common/log.h"

#include <pthread.h>
#include <stddef.h>

#define HAL_ADC_GATE_MAX_CHANNELS 4U

typedef struct {
    bool used;
    int  board_id;
    int  port;
    int  count;
} adc_gate_entry_t;

static adc_gate_entry_t s_entries[HAL_ADC_GATE_MAX_CHANNELS];
static pthread_mutex_t  s_lock = PTHREAD_MUTEX_INITIALIZER;

/* 调用者必须已持有 s_lock */
static adc_gate_entry_t *find_entry_locked(int board_id, int port)
{
    for (size_t i = 0U; i < HAL_ADC_GATE_MAX_CHANNELS; i++) {
        if (s_entries[i].used && (s_entries[i].board_id == board_id) && (s_entries[i].port == port)) {
            return &s_entries[i];
        }
    }
    return NULL;
}

/* 调用者必须已持有 s_lock */
static adc_gate_entry_t *find_or_create_entry_locked(int board_id, int port)
{
    adc_gate_entry_t *entry = find_entry_locked(board_id, port);

    if (entry != NULL) {
        return entry;
    }

    for (size_t i = 0U; i < HAL_ADC_GATE_MAX_CHANNELS; i++) {
        if (!s_entries[i].used) {
            s_entries[i].used     = true;
            s_entries[i].board_id = board_id;
            s_entries[i].port     = port;
            s_entries[i].count    = 0;
            return &s_entries[i];
        }
    }

    LOG_ERROR("hal_adc_gate: table full, board=%d port=%d dropped", board_id, port);
    return NULL;
}

void hal_adc_gate_acquire(int board_id, int port)
{
    adc_gate_entry_t *entry;

    pthread_mutex_lock(&s_lock);
    entry = find_or_create_entry_locked(board_id, port);
    if (entry != NULL) {
        entry->count++;
    }
    pthread_mutex_unlock(&s_lock);
}

void hal_adc_gate_release(int board_id, int port)
{
    adc_gate_entry_t *entry;

    pthread_mutex_lock(&s_lock);
    entry = find_entry_locked(board_id, port);
    if ((entry != NULL) && (entry->count > 0)) {
        entry->count--;
    }
    pthread_mutex_unlock(&s_lock);
}

bool hal_adc_gate_is_needed(int board_id, int port)
{
    adc_gate_entry_t *entry;
    bool              needed;

    pthread_mutex_lock(&s_lock);
    entry  = find_entry_locked(board_id, port);
    needed = (entry != NULL) && (entry->count > 0);
    pthread_mutex_unlock(&s_lock);

    return needed;
}
