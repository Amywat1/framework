/**
 * @file    fluid_path.c
 * @brief   流体路径控制实现（按执行器对账，延时不阻塞其它指令）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "domain/mechanism/patterns/fluid_path.h"

#include "common/log.h"
#include "common/time_util.h"
#include "domain/safety/safety_output_hold.h"

#include <pthread.h>
#include <string.h>

typedef bool (*fluid_path_gate_fn)(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, uint64_t now_ms);

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

static fluid_path_cfg_t          s_cfg;
static fluid_path_actuator_ops_t s_actuator;
static bool                      s_ready = false;

static const fluid_path_def_t *s_paths      = NULL;
static size_t                  s_path_count = 0U;
static fluid_path_mask_t       s_valid_mask = 0U;

static bool              s_desired[FLUID_PATH_CHANNEL_MAX][FLUID_PATH_SLOT_COUNT];
static bool              s_actual[FLUID_PATH_CHANNEL_MAX][FLUID_PATH_SLOT_COUNT];
static uint64_t          s_since_ms[FLUID_PATH_CHANNEL_MAX][FLUID_PATH_SLOT_COUNT];
static bool              s_since_valid[FLUID_PATH_CHANNEL_MAX][FLUID_PATH_SLOT_COUNT];
static fluid_path_mask_t s_pending_target = 0U;
static bool              s_force_off      = false;

static bool path_bit_set(fluid_path_mask_t mask, uint8_t path_idx)
{
    return ((mask & FLUID_PATH_MASK(path_idx)) != 0U);
}

static bool is_pump_slot(fluid_path_slot_t slot)
{
    return (slot == FLUID_PATH_SLOT_PUMP);
}

static bool key_valid_for_channel_count(const fluid_path_actuator_key_t *key, uint8_t channel_count)
{
    return (key != NULL) && (channel_count > 0U) && (key->ch < channel_count)
           && ((unsigned)key->slot < FLUID_PATH_SLOT_COUNT);
}

static bool paths_valid(const fluid_path_cfg_t *cfg,
                        const fluid_path_def_t *paths,
                        size_t                  path_count,
                        fluid_path_mask_t      *out_mask)
{
    fluid_path_mask_t mask = 0U;
    size_t            i;

    if ((cfg == NULL) || (paths == NULL) || (out_mask == NULL) || (path_count == 0U)
        || (path_count > FLUID_PATH_PATH_MAX)) {
        return false;
    }

    for (i = 0U; i < path_count; i++) {
        const fluid_path_def_t *path = &paths[i];
        uint8_t                 j;

        if ((path->path_idx >= FLUID_PATH_PATH_MAX) || (path->deps == NULL) || (path->dep_count == 0U)) {
            return false;
        }
        if ((mask & FLUID_PATH_MASK(path->path_idx)) != 0U) {
            return false;
        }

        for (j = 0U; j < path->dep_count; j++) {
            if (!key_valid_for_channel_count(&path->deps[j], cfg->channel_count)) {
                return false;
            }
        }
        mask |= FLUID_PATH_MASK(path->path_idx);
    }

    *out_mask = mask;
    return true;
}

static bool delay_elapsed(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, uint64_t now_ms, uint32_t delay_ms)
{
    if (!s_since_valid[ch][slot]) {
        return true;
    }
    return time_elapsed_ms(s_since_ms[ch][slot], now_ms) >= delay_ms;
}

static void set_actual(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, bool on, uint64_t now_ms)
{
    sw_err_t ret;

    if (s_actual[ch][slot] == on) {
        return;
    }
    ret = s_actuator.slot_set(ch, slot, on);
    if (ret != SW_OK) {
        LOG_ERROR("fluid_path: ch %d slot %d %s failed ret=%d", (int)ch, (int)slot, on ? "ON" : "OFF", (int)ret);
        return;
    }
    s_actual[ch][slot]      = on;
    s_since_ms[ch][slot]    = now_ms;
    s_since_valid[ch][slot] = true;
}

static void recompute_desired(void)
{
    size_t i;

    memset(s_desired, 0, sizeof(s_desired));
    for (i = 0U; i < s_path_count; i++) {
        const fluid_path_def_t *path = &s_paths[i];
        uint8_t                 j;

        if (!path_bit_set(s_pending_target, path->path_idx)) {
            continue;
        }
        for (j = 0U; j < path->dep_count; j++) {
            s_desired[path->deps[j].ch][path->deps[j].slot] = true;
        }
    }
}

/**
 * @brief  泵可开：目标路径上的阀已到位且满足开阀延时；非目标路径上同泵阀已关
 */
static bool pump_may_turn_on(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, uint64_t now_ms)
{
    size_t i;

    for (i = 0U; i < s_path_count; i++) {
        const fluid_path_def_t *path    = &s_paths[i];
        bool                    uses    = false;
        bool                    blocked = false;
        bool                    wanted  = path_bit_set(s_pending_target, path->path_idx);
        uint8_t                 j;

        for (j = 0U; j < path->dep_count; j++) {
            const fluid_path_actuator_key_t *dep = &path->deps[j];

            if ((dep->ch == ch) && (dep->slot == slot)) {
                uses = true;
            }
            if (is_pump_slot(dep->slot)) {
                continue;
            }
            if (wanted) {
                if (!s_actual[dep->ch][dep->slot]
                    || !delay_elapsed(dep->ch, dep->slot, now_ms, s_cfg.valve_open_delay_ms)) {
                    blocked = true;
                }
            } else if (s_actual[dep->ch][dep->slot]) {
                blocked = true;
            }
        }
        if (uses && blocked) {
            return false;
        }
    }
    return true;
}

/**
 * @brief  阀可关：同路径泵仍在转则可立刻关；泵已停则须满足泄压延时
 */
static bool valve_may_turn_off(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, uint64_t now_ms)
{
    size_t i;

    for (i = 0U; i < s_path_count; i++) {
        const fluid_path_def_t *path    = &s_paths[i];
        bool                    uses    = false;
        bool                    blocked = false;
        uint8_t                 j;

        for (j = 0U; j < path->dep_count; j++) {
            const fluid_path_actuator_key_t *dep = &path->deps[j];

            if ((dep->ch == ch) && (dep->slot == slot)) {
                uses = true;
            }
            if (!is_pump_slot(dep->slot)) {
                continue;
            }
            if (s_actual[dep->ch][dep->slot]) {
                continue;
            }
            if (!delay_elapsed(dep->ch, dep->slot, now_ms, s_cfg.pump_stop_delay_ms)) {
                blocked = true;
            }
        }
        if (uses && blocked) {
            return false;
        }
    }
    return true;
}

static void apply_changes(bool pumps, bool on, fluid_path_gate_fn gate, uint64_t now_ms)
{
    fluid_path_channel_idx_t ch;
    unsigned                 slot;

    for (ch = 0U; ch < s_cfg.channel_count; ch++) {
        for (slot = 0U; slot < (unsigned)FLUID_PATH_SLOT_COUNT; slot++) {
            if (is_pump_slot((fluid_path_slot_t)slot) != pumps) {
                continue;
            }
            if ((s_actual[ch][slot] == on) || (s_desired[ch][slot] != on)) {
                continue;
            }
            if ((gate != NULL) && !gate(ch, (fluid_path_slot_t)slot, now_ms)) {
                continue;
            }
            set_actual(ch, (fluid_path_slot_t)slot, on, now_ms);
        }
    }
}

static void reset_state(void)
{
    memset(s_desired, 0, sizeof(s_desired));
    memset(s_actual, 0, sizeof(s_actual));
    memset(s_since_ms, 0, sizeof(s_since_ms));
    memset(s_since_valid, 0, sizeof(s_since_valid));
    s_pending_target = 0U;
    s_force_off      = false;
}

static void force_off_locked(void)
{
    if (s_actuator.all_off != NULL) {
        (void)s_actuator.all_off();
    }
    reset_state();
}

static bool outputs_match_desired(void)
{
    fluid_path_channel_idx_t ch;
    unsigned                 slot;

    for (ch = 0U; ch < s_cfg.channel_count; ch++) {
        for (slot = 0U; slot < (unsigned)FLUID_PATH_SLOT_COUNT; slot++) {
            if (s_actual[ch][slot] != s_desired[ch][slot]) {
                return false;
            }
        }
    }
    return true;
}

static void tick_locked(uint64_t now_ms)
{
    if (safety_output_hold_is_active()) {
        force_off_locked();
        return;
    }

    if (s_force_off) {
        force_off_locked();
        return;
    }

    recompute_desired();
    apply_changes(true, false, NULL, now_ms);                /* 关泵 */
    apply_changes(false, true, NULL, now_ms);                /* 开阀 */
    apply_changes(false, false, valve_may_turn_off, now_ms); /* 关阀 */
    apply_changes(true, true, pump_may_turn_on, now_ms);     /* 开泵 */
}

sw_err_t fluid_path_init(const fluid_path_cfg_t          *cfg,
                         const fluid_path_actuator_ops_t *ops,
                         const fluid_path_def_t          *paths,
                         size_t                           path_count)
{
    fluid_path_mask_t valid_mask = 0U;

    if ((cfg == NULL) || (ops == NULL) || (ops->slot_set == NULL) || (paths == NULL) || (path_count == 0U)
        || (cfg->channel_count == 0U) || (cfg->channel_count > FLUID_PATH_CHANNEL_MAX)) {
        return SW_ERR_PARAM;
    }
    if (!paths_valid(cfg, paths, path_count, &valid_mask)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    s_cfg        = *cfg;
    s_actuator   = *ops;
    s_paths      = paths;
    s_path_count = path_count;
    s_valid_mask = valid_mask;
    s_ready      = true;
    force_off_locked();
    pthread_mutex_unlock(&s_mutex);

    /* 领域层不创建线程：时序推进由调用方登记 fluid_path_poll 为周期任务驱动。 */
    return SW_OK;
}

/**
 * @brief  持锁校验就绪、拓扑掩码与输出抑制
 * @note   调用方已持 s_mutex。失败时不解锁。
 */
static sw_err_t target_cmd_ok_locked(fluid_path_mask_t bits)
{
    if (!s_ready) {
        return SW_ERR_NOT_INIT;
    }
    if ((bits & ~s_valid_mask) != 0U) {
        return SW_ERR_PARAM;
    }
    if (safety_output_hold_is_active()) {
        return SW_ERR_STATE;
    }
    return SW_OK;
}

sw_err_t fluid_path_set(fluid_path_mask_t target)
{
    sw_err_t err;

    pthread_mutex_lock(&s_mutex);
    err = target_cmd_ok_locked(target);
    if (err == SW_OK) {
        s_pending_target = target;
    }
    pthread_mutex_unlock(&s_mutex);
    return err;
}

sw_err_t fluid_path_enable(fluid_path_mask_t mask)
{
    sw_err_t err;

    pthread_mutex_lock(&s_mutex);
    err = target_cmd_ok_locked(mask);
    if (err == SW_OK) {
        s_pending_target |= mask;
    }
    pthread_mutex_unlock(&s_mutex);
    return err;
}

sw_err_t fluid_path_disable(fluid_path_mask_t mask)
{
    sw_err_t err;

    pthread_mutex_lock(&s_mutex);
    err = target_cmd_ok_locked(mask);
    if (err == SW_OK) {
        s_pending_target &= ~mask;
    }
    pthread_mutex_unlock(&s_mutex);
    return err;
}

sw_err_t fluid_path_all_off(void)
{
    pthread_mutex_lock(&s_mutex);
    if (!s_ready) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    s_force_off      = true;
    s_pending_target = 0U;
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

void fluid_path_poll(uint64_t now_ms)
{
    pthread_mutex_lock(&s_mutex);
    if (s_ready) {
        tick_locked(now_ms);
    }
    pthread_mutex_unlock(&s_mutex);
}

bool fluid_path_is_settled(void)
{
    bool settled;

    if (safety_output_hold_is_active()) {
        return false;
    }
    pthread_mutex_lock(&s_mutex);
    if (!s_ready || s_force_off || safety_output_hold_is_active()) {
        settled = false;
    } else {
        recompute_desired();
        settled = outputs_match_desired();
    }
    pthread_mutex_unlock(&s_mutex);
    return settled;
}
