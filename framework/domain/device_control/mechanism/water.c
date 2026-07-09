/**
 * @file    water.c
 * @brief   水路控制实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/domain/device_control/mechanism/water.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/runtime/scheduler/periodic_task.h"
#include "framework/common/log.h"
#include "framework/common/time_util.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <string.h>

#define WATER_ACTUATOR_LIST_MAX  16U

typedef enum
{
    WATER_SEQ_IDLE = 0,
    WATER_SEQ_WAIT_CLOSE_PUMP,
    WATER_SEQ_WAIT_OPEN_VALVE,
} water_seq_state_t;

static pthread_mutex_t    s_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool        s_emergency_off = false;

static water_cfg_t           s_cfg;
static water_actuator_ops_t  s_actuator;
static bool                  s_ready = false;
static uint8_t               s_channel_count = 0U;

static const water_path_def_t *s_paths = NULL;
static size_t                  s_path_count = 0U;

static uint8_t           s_ref[WATER_CHANNEL_MAX][WATER_SLOT_COUNT];
static bool              s_actual[WATER_CHANNEL_MAX][WATER_SLOT_COUNT];
static water_path_mask_t s_pending_target = 0U;
static water_path_mask_t s_stable_paths   = 0U;

static water_seq_state_t    s_seq_state   = WATER_SEQ_IDLE;
static uint32_t             s_deadline_ms = 0U;
static water_path_mask_t    s_closing_mask = 0U;
static water_path_mask_t    s_opening_mask = 0U;
static water_actuator_key_t s_work_list[WATER_ACTUATOR_LIST_MAX];
static uint8_t              s_work_count = 0U;
static bool                 s_force_off  = false;

static bool path_bit_set(water_path_mask_t mask, uint8_t path_idx)
{
    return ((mask & WATER_PATH_MASK(path_idx)) != 0U);
}

static bool is_pump_slot(water_slot_t slot)
{
    return (slot == WATER_SLOT_PUMP);
}

static bool key_valid(const water_actuator_key_t *key)
{
    return (key != NULL)
           && (s_channel_count > 0U)
           && (key->ch < s_channel_count)
           && ((unsigned)key->slot < WATER_SLOT_COUNT);
}

static bool list_has(const water_actuator_key_t *key)
{
    uint8_t i;

    for (i = 0U; i < s_work_count; i++)
    {
        if ((s_work_list[i].ch == key->ch) && (s_work_list[i].slot == key->slot))
        {
            return true;
        }
    }
    return false;
}

static void list_add(const water_actuator_key_t *key)
{
    if ((s_work_count >= WATER_ACTUATOR_LIST_MAX) || !key_valid(key) || list_has(key))
    {
        return;
    }
    s_work_list[s_work_count] = *key;
    s_work_count++;
}

static bool work_has_pump(void)
{
    uint8_t i;

    for (i = 0U; i < s_work_count; i++)
    {
        if (is_pump_slot(s_work_list[i].slot))
        {
            return true;
        }
    }
    return false;
}

static bool work_has_valve(void)
{
    uint8_t i;

    for (i = 0U; i < s_work_count; i++)
    {
        if (!is_pump_slot(s_work_list[i].slot))
        {
            return true;
        }
    }
    return false;
}

static sw_err_t output_slot(water_channel_idx_t ch, water_slot_t slot, bool on)
{
    sw_err_t ret;

    if (s_actuator.slot_set == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    ret = s_actuator.slot_set(ch, slot, on);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: ch %d slot %d %s failed ret=%d",
                  (int)ch, (int)slot, on ? "ON" : "OFF", (int)ret);
    }
    return ret;
}

static sw_err_t apply_group(bool pump_group, bool on)
{
    sw_err_t ret;
    sw_err_t first_err = SW_OK;
    uint8_t  i;

    for (i = 0U; i < s_work_count; i++)
    {
        if (is_pump_slot(s_work_list[i].slot) != pump_group)
        {
            continue;
        }
        ret = output_slot(s_work_list[i].ch, s_work_list[i].slot, on);
        if (ret == SW_OK)
        {
            s_actual[s_work_list[i].ch][s_work_list[i].slot] = on;
        }
        else if (first_err == SW_OK)
        {
            first_err = ret;
        }
    }
    return first_err;
}

static void collect_deps(water_path_mask_t mask, bool opening)
{
    size_t i;

    s_work_count = 0U;
    for (i = 0U; i < s_path_count; i++)
    {
        const water_path_def_t *path = &s_paths[i];
        uint8_t                 j;

        if (!path_bit_set(mask, path->path_idx))
        {
            continue;
        }
        for (j = 0U; j < path->dep_count; j++)
        {
            const water_actuator_key_t *dep = &path->deps[j];

            if (!key_valid(dep))
            {
                continue;
            }
            if (opening)
            {
                if (s_ref[dep->ch][dep->slot] == 0U)
                {
                    list_add(dep);
                }
            }
            else if (s_ref[dep->ch][dep->slot] == 1U)
            {
                list_add(dep);
            }
        }
    }
}

static void add_refs(water_path_mask_t mask)
{
    size_t i;

    for (i = 0U; i < s_path_count; i++)
    {
        const water_path_def_t *path = &s_paths[i];
        uint8_t                 j;

        if (!path_bit_set(mask, path->path_idx))
        {
            continue;
        }
        for (j = 0U; j < path->dep_count; j++)
        {
            if (key_valid(&path->deps[j]))
            {
                s_ref[path->deps[j].ch][path->deps[j].slot]++;
            }
        }
    }
}

static void sub_refs(water_path_mask_t mask)
{
    size_t i;

    for (i = 0U; i < s_path_count; i++)
    {
        const water_path_def_t *path = &s_paths[i];
        uint8_t                 j;

        if (!path_bit_set(mask, path->path_idx))
        {
            continue;
        }
        for (j = 0U; j < path->dep_count; j++)
        {
            if (key_valid(&path->deps[j]) && (s_ref[path->deps[j].ch][path->deps[j].slot] > 0U))
            {
                s_ref[path->deps[j].ch][path->deps[j].slot]--;
            }
        }
    }
}

static void reset_state(void)
{
    memset(s_ref, 0, sizeof(s_ref));
    memset(s_actual, 0, sizeof(s_actual));
    s_pending_target = 0U;
    s_stable_paths   = 0U;
    s_seq_state      = WATER_SEQ_IDLE;
    s_deadline_ms    = 0U;
    s_closing_mask   = 0U;
    s_opening_mask   = 0U;
    s_work_count     = 0U;
    s_force_off      = false;
}

static void force_off_locked(void)
{
    if (s_actuator.all_off != NULL)
    {
        (void)s_actuator.all_off();
    }
    reset_state();
}

static sw_err_t close_pumps_locked(uint32_t now_ms)
{
    sw_err_t ret;

    if (!work_has_pump())
    {
        return SW_OK;
    }
    ret = apply_group(true, false);
    if ((ret != SW_OK) || (s_cfg.pump_stop_delay_ms == 0U))
    {
        return ret;
    }
    s_seq_state   = WATER_SEQ_WAIT_CLOSE_PUMP;
    s_deadline_ms = now_ms;
    return SW_OK;
}

static sw_err_t close_valves_locked(void)
{
    sw_err_t ret = SW_OK;

    if (work_has_valve())
    {
        ret = apply_group(false, false);
    }
    s_stable_paths &= ~s_closing_mask;
    s_closing_mask    = 0U;
    s_work_count      = 0U;
    s_seq_state       = WATER_SEQ_IDLE;
    return ret;
}

static sw_err_t begin_close_locked(water_path_mask_t to_close, uint32_t now_ms)
{
    sw_err_t ret;

    collect_deps(to_close, false);
    sub_refs(to_close);
    s_closing_mask = to_close;

    if (s_work_count == 0U)
    {
        s_stable_paths &= ~to_close;
        return SW_OK;
    }

    ret = close_pumps_locked(now_ms);
    if (ret != SW_OK)
    {
        return ret;
    }
    if (s_seq_state == WATER_SEQ_IDLE)
    {
        return close_valves_locked();
    }
    return SW_OK;
}

static sw_err_t open_valves_locked(uint32_t now_ms)
{
    sw_err_t ret;

    if (!work_has_valve())
    {
        return SW_OK;
    }
    ret = apply_group(false, true);
    if ((ret != SW_OK) || (s_cfg.valve_open_delay_ms == 0U))
    {
        return ret;
    }
    s_seq_state   = WATER_SEQ_WAIT_OPEN_VALVE;
    s_deadline_ms = now_ms;
    return SW_OK;
}

static sw_err_t open_pumps_locked(water_path_mask_t opened_mask)
{
    sw_err_t ret = SW_OK;

    if (work_has_pump())
    {
        ret = apply_group(true, true);
    }
    if (ret == SW_OK)
    {
        s_stable_paths |= opened_mask;
        s_work_count      = 0U;
        s_seq_state       = WATER_SEQ_IDLE;
    }
    return ret;
}

static sw_err_t begin_open_locked(water_path_mask_t to_open, uint32_t now_ms)
{
    collect_deps(to_open, true);
    add_refs(to_open);

    if (s_work_count == 0U)
    {
        s_stable_paths |= to_open;
        return SW_OK;
    }

    if (open_valves_locked(now_ms) != SW_OK)
    {
        sub_refs(to_open);
        return SW_ERR_HW;
    }

    s_opening_mask = to_open;
    if (s_seq_state == WATER_SEQ_IDLE)
    {
        return open_pumps_locked(to_open);
    }
    return SW_OK;
}

static void process_idle_locked(uint32_t now_ms)
{
    water_path_mask_t to_close = s_stable_paths & ~s_pending_target;
    water_path_mask_t to_open  = s_pending_target & ~s_stable_paths;

    if (to_close != 0U)
    {
        if (begin_close_locked(to_close, now_ms) != SW_OK)
        {
            return;
        }
        if (s_seq_state != WATER_SEQ_IDLE)
        {
            return;
        }
        to_open = s_pending_target & ~s_stable_paths;
    }
    if (to_open != 0U)
    {
        (void)begin_open_locked(to_open, now_ms);
    }
}

static void tick_locked(uint32_t now_ms)
{
    if (atomic_exchange_explicit(&s_emergency_off, false, memory_order_acq_rel))
    {
        s_force_off = false;
        force_off_locked();
        return;
    }

    if (s_force_off)
    {
        force_off_locked();
        return;
    }

    switch (s_seq_state)
    {
    case WATER_SEQ_WAIT_CLOSE_PUMP:
        if (time_elapsed_ms(s_deadline_ms, now_ms) >= s_cfg.pump_stop_delay_ms)
        {
            (void)close_valves_locked();
        }
        break;

    case WATER_SEQ_WAIT_OPEN_VALVE:
        if (time_elapsed_ms(s_deadline_ms, now_ms) >= s_cfg.valve_open_delay_ms)
        {
            (void)open_pumps_locked(s_opening_mask);
            s_opening_mask = 0U;
        }
        break;

    case WATER_SEQ_IDLE:
    default:
        if (s_pending_target != s_stable_paths)
        {
            process_idle_locked(now_ms);
        }
        break;
    }
}

#ifndef WATER_UNIT_TEST

static void water_poll_task(void *ctx)
{
    (void)ctx;

    pthread_mutex_lock(&s_mutex);
    if (s_ready)
    {
        tick_locked(time_util_get_ms());
    }
    pthread_mutex_unlock(&s_mutex);
}

static sw_err_t poll_task_register(void)
{
    sw_err_t ret;

    ret = periodic_task_register("water_poll",
                                 THD_WATER_POLL_PERIOD_MS,
                                 water_poll_task,
                                 NULL,
                                 SCHED_OTHER,
                                 THD_WATER_POLL_NICE,
                                 THD_WATER_POLL_STACK);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: periodic_task_register failed ret=%d", (int)ret);
    }
    return ret;
}

#endif /* WATER_UNIT_TEST */

sw_err_t water_init(const water_cfg_t *cfg,
                    const water_actuator_ops_t *ops,
                    const water_path_def_t *paths,
                    size_t path_count)
{
    if ((cfg == NULL) || (ops == NULL) || (ops->slot_set == NULL)
        || (paths == NULL) || (path_count == 0U)
        || (cfg->channel_count == 0U) || (cfg->channel_count > WATER_CHANNEL_MAX))
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    s_cfg           = *cfg;
    s_channel_count = cfg->channel_count;
    s_actuator   = *ops;
    s_paths      = paths;
    s_path_count = path_count;
    s_ready      = true;
    reset_state();
    force_off_locked();
    pthread_mutex_unlock(&s_mutex);

#ifndef WATER_UNIT_TEST
    return poll_task_register();
#else
    return SW_OK;
#endif
}

sw_err_t water_path_set(water_path_mask_t target)
{
    pthread_mutex_lock(&s_mutex);
    if (!s_ready)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    s_pending_target = target;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

void water_emergency_off(void)
{
    atomic_store_explicit(&s_emergency_off, true, memory_order_release);
}

sw_err_t water_all_off(void)
{
    pthread_mutex_lock(&s_mutex);
    if (!s_ready)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    s_force_off      = true;
    s_pending_target = 0U;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

#ifdef WATER_UNIT_TEST

void water_poll(uint32_t now_ms)
{
    pthread_mutex_lock(&s_mutex);
    if (s_ready)
    {
        tick_locked(now_ms);
    }
    pthread_mutex_unlock(&s_mutex);
}

bool water_is_settled(void)
{
    bool settled;

    pthread_mutex_lock(&s_mutex);
    settled = (!s_force_off)
              && (s_seq_state == WATER_SEQ_IDLE)
              && (s_pending_target == s_stable_paths);
    pthread_mutex_unlock(&s_mutex);
    return settled;
}

#endif /* WATER_UNIT_TEST */
