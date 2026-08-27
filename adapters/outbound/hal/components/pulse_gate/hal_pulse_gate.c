/**
 * @file    hal_pulse_gate.c
 * @brief   脉冲计数器采样按需门控实现
 * @author  HUWANGWEI
 * @date    2026-08-27
 *
 * @note    共享状态由 s_lock 保护，acquire/release/is_needed 可在不同线程中并发调用。
 */

#include "adapters/outbound/hal/components/pulse_gate/hal_pulse_gate.h"

#include "common/log.h"

#include <pthread.h>
#include <stddef.h>
#include <string.h>

#define HAL_PULSE_GATE_MAX_CHANNELS 8U

typedef struct {
    bool used;
    int  board_id;
    int  pin_id;
    int  count;
} pulse_gate_entry_t;

static pulse_gate_entry_t s_entries[HAL_PULSE_GATE_MAX_CHANNELS];
static pthread_mutex_t    s_lock = PTHREAD_MUTEX_INITIALIZER;

/* 调用者必须已持有 s_lock */
static pulse_gate_entry_t *find_entry_locked(int board_id, int pin_id)
{
    size_t i;

    for (i = 0U; i < HAL_PULSE_GATE_MAX_CHANNELS; i++) {
        if (s_entries[i].used && (s_entries[i].board_id == board_id) && (s_entries[i].pin_id == pin_id)) {
            return &s_entries[i];
        }
    }
    return NULL;
}

/* 调用者必须已持有 s_lock */
static pulse_gate_entry_t *find_or_create_entry_locked(int board_id, int pin_id)
{
    pulse_gate_entry_t *entry = find_entry_locked(board_id, pin_id);
    size_t              i;

    if (entry != NULL) {
        return entry;
    }

    for (i = 0U; i < HAL_PULSE_GATE_MAX_CHANNELS; i++) {
        if (!s_entries[i].used) {
            s_entries[i].used     = true;
            s_entries[i].board_id = board_id;
            s_entries[i].pin_id   = pin_id;
            s_entries[i].count    = 0;
            return &s_entries[i];
        }
    }

    LOG_ERROR("hal_pulse_gate: table full, board=%d pin=%d dropped", board_id, pin_id);
    return NULL;
}

void hal_pulse_gate_acquire(int board_id, int pin_id)
{
    pulse_gate_entry_t *entry;

    pthread_mutex_lock(&s_lock);
    entry = find_or_create_entry_locked(board_id, pin_id);
    if (entry != NULL) {
        entry->count++;
    }
    pthread_mutex_unlock(&s_lock);
}

void hal_pulse_gate_release(int board_id, int pin_id)
{
    pulse_gate_entry_t *entry;

    pthread_mutex_lock(&s_lock);
    entry = find_entry_locked(board_id, pin_id);
    if ((entry != NULL) && (entry->count > 0)) {
        entry->count--;
    }
    pthread_mutex_unlock(&s_lock);
}

bool hal_pulse_gate_is_needed(int board_id, int pin_id)
{
    pulse_gate_entry_t *entry;
    bool                needed;

    pthread_mutex_lock(&s_lock);
    entry  = find_entry_locked(board_id, pin_id);
    needed = (entry != NULL) && (entry->count > 0);
    pthread_mutex_unlock(&s_lock);

    return needed;
}

void hal_pulse_gate_reset_for_test(void)
{
    pthread_mutex_lock(&s_lock);
    memset(s_entries, 0, sizeof(s_entries));
    pthread_mutex_unlock(&s_lock);
}
