/**
 * @file    hal_vfd_manager.c
 * @brief   VFD HAL 通用组合层实现
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"

#include "common/log.h"
#include "common/pulse_out.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <pthread.h>
#include <sched.h>
#include <string.h>

/** @brief 连续 Modbus 失败 N 次后上报 COMM_LOST */
#define VFD_COMM_FAIL_NOTIFY 3U

/** @brief 工作项：槽位 × 2 + 通道（0=故障码，1=电流） */
#define VFD_CH_FAULT   0U
#define VFD_CH_CURRENT 1U
#define VFD_CH_COUNT   2U
#define VFD_WORK_MAX   (HAL_VFD_MANAGER_SLOT_MAX * VFD_CH_COUNT)

typedef struct {
    bool                       bound;
    bool                       initialized;
    hal_vfd_manager_bind_cfg_t cfg;
    void (*event_cb)(int event_code);

    uint16_t cached_fault_code;
    uint16_t cached_current;
    bool     fault_active;
    bool     comm_ok;
    uint8_t  comm_fail_count;
    uint64_t last_fault_ms;
    uint64_t last_current_ms;

    pulse_out_slot_t rst_pulse;
    enum {
        VFD_CONTROL_NONE = 0,
        VFD_CONTROL_GEAR,
        VFD_CONTROL_FREQUENCY,
    } control_mode;
} hal_vfd_slot_t;

static hal_vfd_slot_t s_slot[HAL_VFD_MANAGER_SLOT_MAX];

static unsigned s_fast_rr;
static unsigned s_bg_rr;
static uint8_t  s_fast_since_keepalive;
static uint64_t s_fast_lag_warn_ms;

/* 两把锁均位于急停切断热路径：safety_cutout_execute -> 项目 cutout 实现 ->
 * 项目电机紧急切断 -> 驱动 cutoff -> 本模块停机。该路径由 estop_poll
 * 线程以 SCHED_FIFO 高优先级执行，而两把锁又被 SCHED_OTHER 周期任务（变频器
 * 轮询、通信保活）竞争，故启用优先级继承。本模块无 init 入口，用 pthread_once
 * 在首次上锁前完成初始化。 */

/** @brief 保护上面除 backend 调用以外的全部运行时簿记字段 */
static pthread_mutex_t s_vfd_lock;

/** @brief 串行化控制命令，保证模式检查、硬件输出与模式提交不可交叉 */
static pthread_mutex_t s_control_lock;

static pthread_once_t s_mutex_once = PTHREAD_ONCE_INIT;

static void vfd_mutex_init_once(void)
{
    (void)sw_mutex_init_prio_inherit(&s_vfd_lock);
    (void)sw_mutex_init_prio_inherit(&s_control_lock);
}

/** @brief 确保两把锁已初始化（幂等，所有加锁点入口调用）*/
static void vfd_mutexes_ready(void)
{
    (void)pthread_once(&s_mutex_once, vfd_mutex_init_once);
}

static sw_err_t vfd_stop(hal_vfd_id_t id);

static bool slot_id_valid(hal_vfd_id_t id)
{
    return ((id >= 0) && ((unsigned)id < HAL_VFD_MANAGER_SLOT_MAX));
}

static hal_vfd_slot_t *slot_by_id(hal_vfd_id_t id)
{
    if (!slot_id_valid(id) || !s_slot[(unsigned)id].bound) {
        return NULL;
    }
    return &s_slot[(unsigned)id];
}

static hal_vfd_slot_t *ready_slot_by_id(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !slot->initialized) {
        return NULL;
    }
    return slot;
}

static bool channel_policy_valid(const hal_vfd_channel_policy_t *policy)
{
    if (policy == NULL) {
        return false;
    }
    switch (policy->class) {
    case HAL_VFD_SAMPLE_OFF:
        return (policy->period_ms == 0U);
    case HAL_VFD_SAMPLE_BACKGROUND:
    case HAL_VFD_SAMPLE_FAST:
        return (policy->period_ms > 0U);
    default:
        return false;
    }
}

static bool bind_cfg_valid(const hal_vfd_manager_bind_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->ops == NULL) || (cfg->drv_ctx == NULL)) {
        return false;
    }
    if ((cfg->ops->apply_gear == NULL) || (cfg->ops->apply_frequency == NULL) || (cfg->ops->stop_outputs == NULL)
        || (cfg->ops->set_rst == NULL) || (cfg->ops->read == NULL) || (cfg->ops->get_state == NULL)) {
        return false;
    }
    if ((cfg->ops->clear_fault == NULL) != (cfg->ops->has_clear_fault == NULL)) {
        return false;
    }
    if (cfg->rst_pulse_ms == 0U) {
        return false;
    }
    return channel_policy_valid(&cfg->fault) && channel_policy_valid(&cfg->current);
}

static sw_err_t pulse_set_rst_level(void *ctx, bool level)
{
    hal_vfd_slot_t *slot = (hal_vfd_slot_t *)ctx;

    if (slot == NULL) {
        return SW_ERR_PARAM;
    }
    return slot->cfg.ops->set_rst(slot->cfg.drv_ctx, level);
}

/** @brief 读出事件回调并在锁外调用，避免回调重入本模块查询接口时自锁死锁 */
static void emit_event(hal_vfd_slot_t *slot, int event_code)
{
    void (*cb)(int) = NULL;

    if (slot == NULL) {
        return;
    }

    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    cb = slot->event_cb;
    pthread_mutex_unlock(&s_vfd_lock);

    if (cb != NULL) {
        cb(event_code);
    }
}

/** @brief 调用方须已持有 s_vfd_lock；返回 true 表示需要上报 COMM_RESTORED */
static bool on_comm_success_locked(hal_vfd_slot_t *slot)
{
    bool restored = false;

    if (!slot->comm_ok) {
        slot->comm_ok = true;
        restored      = true;
    }
    slot->comm_fail_count = 0U;
    return restored;
}

/** @brief 调用方须已持有 s_vfd_lock；返回 true 表示需要上报 COMM_LOST */
static bool on_comm_failure_locked(hal_vfd_slot_t *slot)
{
    bool lost = false;

    if (slot->comm_fail_count < 0xFFU) {
        slot->comm_fail_count++;
    }

    if ((slot->comm_fail_count >= VFD_COMM_FAIL_NOTIFY) && slot->comm_ok) {
        slot->comm_ok = false;
        lost          = true;
    }
    return lost;
}

/** @brief 调用方须已持有 s_vfd_lock；返回需要上报的事件码，0 表示无事件 */
static int update_fault_state_locked(hal_vfd_slot_t *slot, uint16_t code)
{
    bool now_fault;

    slot->cached_fault_code = code;
    now_fault               = (code != 0U);

    if (now_fault && !slot->fault_active) {
        slot->fault_active = true;
        return HAL_VFD_EVT_FAULT_DETECTED;
    }
    if (!now_fault && slot->fault_active) {
        slot->fault_active = false;
        return HAL_VFD_EVT_FAULT_CLEARED;
    }
    return 0;
}

static bool monitor_sample_fault(hal_vfd_slot_t *slot, uint64_t now_ms)
{
    sw_err_t ret;
    uint16_t val = 0U;
    bool     comm_restored;
    bool     comm_lost;
    int      fault_evt;

    ret = slot->cfg.ops->read(slot->cfg.drv_ctx, HAL_VFD_REG_FAULT_CODE, &val);

    comm_restored = false;
    comm_lost     = false;
    fault_evt     = 0;

    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    if (ret == SW_OK) {
        comm_restored = on_comm_success_locked(slot);
        fault_evt     = update_fault_state_locked(slot, val);
    } else if (ret == SW_ERR_COMM) {
        comm_lost = on_comm_failure_locked(slot);
    }
    slot->last_fault_ms = now_ms;
    pthread_mutex_unlock(&s_vfd_lock);

    if (comm_restored) {
        emit_event(slot, HAL_VFD_EVT_COMM_RESTORED);
    }
    if (comm_lost) {
        emit_event(slot, HAL_VFD_EVT_COMM_LOST);
    }
    if (fault_evt != 0) {
        emit_event(slot, fault_evt);
    }
    return (ret == SW_OK);
}

static bool monitor_sample_current(hal_vfd_slot_t *slot, uint64_t now_ms)
{
    hal_vfd_state_t st;
    sw_err_t        ret;
    uint16_t        val           = 0U;
    bool            comm_restored = false;
    bool            comm_lost     = false;

    st = slot->cfg.ops->get_state(slot->cfg.drv_ctx);
    if ((st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV)) {
        ret = slot->cfg.ops->read(slot->cfg.drv_ctx, HAL_VFD_REG_CURRENT, &val);

        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        if (ret == SW_OK) {
            comm_restored        = on_comm_success_locked(slot);
            slot->cached_current = val;
        } else if (ret == SW_ERR_COMM) {
            comm_lost = on_comm_failure_locked(slot);
        }
        slot->last_current_ms = now_ms;
        pthread_mutex_unlock(&s_vfd_lock);

        if (comm_restored) {
            emit_event(slot, HAL_VFD_EVT_COMM_RESTORED);
        }
        if (comm_lost) {
            emit_event(slot, HAL_VFD_EVT_COMM_LOST);
        }
        if (ret == SW_OK) {
            emit_event(slot, HAL_VFD_EVT_CURRENT_UPDATE);
        }
        return (ret == SW_OK);
    }

    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    slot->last_current_ms = now_ms;
    pthread_mutex_unlock(&s_vfd_lock);
    return false;
}

static void maybe_warn_fast_lag(uint32_t period_ms, uint32_t actual_ms)
{
    uint64_t now_ms;

    if (actual_ms <= period_ms) {
        return;
    }

    now_ms = time_util_get_ms();
    if ((s_fast_lag_warn_ms != 0U)
        && (time_elapsed_ms(s_fast_lag_warn_ms, now_ms) < HAL_VFD_FAST_LAG_WARN_INTERVAL_MS)) {
        return;
    }
    s_fast_lag_warn_ms = now_ms;
    LOG_WARN("hal_vfd: FAST sample interval %ums > period %ums", (unsigned)actual_ms, (unsigned)period_ms);
}

static void note_fast_sample(uint64_t last_ms, uint64_t now_ms, uint32_t period_ms)
{
    uint32_t gap;

    if ((last_ms == 0U) || (now_ms <= last_ms)) {
        return;
    }
    gap = (uint32_t)time_elapsed_ms(last_ms, now_ms);
    maybe_warn_fast_lag(period_ms, gap);
}

sw_err_t hal_vfd_manager_bind(hal_vfd_id_t id, const hal_vfd_manager_bind_cfg_t *cfg)
{
    hal_vfd_slot_t *slot;

    if (!slot_id_valid(id) || !bind_cfg_valid(cfg)) {
        return SW_ERR_PARAM;
    }

    slot = &s_slot[(unsigned)id];

    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    {
        void (*saved_cb)(int) = slot->event_cb;

        if (slot->bound) {
            pthread_mutex_unlock(&s_vfd_lock);
            return SW_ERR_BUSY;
        }

        slot->cfg         = *cfg;
        slot->bound       = true;
        slot->initialized = false;
        slot->event_cb    = saved_cb;
    }
    slot->cached_fault_code   = 0U;
    slot->cached_current      = 0U;
    slot->fault_active        = false;
    slot->comm_ok             = true;
    slot->comm_fail_count     = 0U;
    slot->last_fault_ms       = 0U;
    slot->last_current_ms     = 0U;
    slot->rst_pulse.ctx       = slot;
    slot->rst_pulse.set_level = pulse_set_rst_level;
    slot->rst_pulse.active    = false;
    slot->control_mode        = VFD_CONTROL_NONE;
    pthread_mutex_unlock(&s_vfd_lock);
    return SW_OK;
}

static sw_err_t vfd_init(void)
{
    unsigned i;
    sw_err_t first_ret = SW_OK;

    for (i = 0U; i < HAL_VFD_MANAGER_SLOT_MAX; i++) {
        hal_vfd_manager_bind_cfg_t cfg;
        bool                       bound;
        sw_err_t                   ret = SW_OK;

        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        bound = s_slot[i].bound;
        cfg   = s_slot[i].cfg;
        pthread_mutex_unlock(&s_vfd_lock);

        if (!bound) {
            continue;
        }

        if (cfg.ops->init != NULL) {
            ret = cfg.ops->init(cfg.drv_ctx);
        }

        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        pulse_out_cancel(&s_slot[i].rst_pulse);
        s_slot[i].cached_fault_code = 0U;
        s_slot[i].cached_current    = 0U;
        s_slot[i].fault_active      = false;
        s_slot[i].comm_ok           = true;
        s_slot[i].comm_fail_count   = 0U;
        s_slot[i].last_fault_ms     = 0U;
        s_slot[i].last_current_ms   = 0U;
        s_slot[i].control_mode      = VFD_CONTROL_NONE;
        s_slot[i].initialized       = (ret == SW_OK);
        pthread_mutex_unlock(&s_vfd_lock);

        if ((ret != SW_OK) && (first_ret == SW_OK)) {
            first_ret = ret;
        }
    }
    return first_ret;
}

typedef struct {
    bool                     valid;
    bool                     running;
    hal_vfd_channel_policy_t fault;
    hal_vfd_channel_policy_t current;
    uint64_t                 last_fault_ms;
    uint64_t                 last_current_ms;
} vfd_slot_view_t;

static void take_slot_views(vfd_slot_view_t views[HAL_VFD_MANAGER_SLOT_MAX])
{
    unsigned i;

    for (i = 0U; i < HAL_VFD_MANAGER_SLOT_MAX; i++) {
        views[i].valid = false;
        if (!s_slot[i].bound || !s_slot[i].initialized) {
            continue;
        }
        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        views[i].valid          = true;
        views[i].last_fault_ms   = s_slot[i].last_fault_ms;
        views[i].last_current_ms = s_slot[i].last_current_ms;
        views[i].fault           = s_slot[i].cfg.fault;
        views[i].current         = s_slot[i].cfg.current;
        pthread_mutex_unlock(&s_vfd_lock);
        views[i].running = false;
        if (s_slot[i].cfg.ops->get_state != NULL) {
            hal_vfd_state_t st = s_slot[i].cfg.ops->get_state(s_slot[i].cfg.drv_ctx);
            views[i].running   = ((st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV));
        }
    }
}

static bool work_is_due(const vfd_slot_view_t *views, unsigned enc, bool want_fast, uint64_t now_ms)
{
    unsigned                     slot;
    unsigned                     ch;
    const vfd_slot_view_t       *v;
    const hal_vfd_channel_policy_t *policy;
    uint64_t                     last_ms;
    bool                         suppress_fault;

    slot = enc / VFD_CH_COUNT;
    ch   = enc % VFD_CH_COUNT;
    v    = &views[slot];
    if (!v->valid) {
        return false;
    }

    suppress_fault = v->running && (v->current.class == HAL_VFD_SAMPLE_FAST);
    if (ch == VFD_CH_FAULT) {
        policy = &v->fault;
        last_ms = v->last_fault_ms;
        if (suppress_fault) {
            return false;
        }
    } else {
        policy = &v->current;
        last_ms = v->last_current_ms;
        if (!v->running) {
            return false;
        }
    }

    if (want_fast) {
        if (policy->class != HAL_VFD_SAMPLE_FAST) {
            return false;
        }
    } else if (policy->class != HAL_VFD_SAMPLE_BACKGROUND) {
        return false;
    }

    return (time_elapsed_ms(last_ms, now_ms) >= policy->period_ms);
}

static bool pick_due_work(const vfd_slot_view_t *views,
                          unsigned              *cursor,
                          bool                   want_fast,
                          uint64_t               now_ms,
                          unsigned              *out_enc)
{
    unsigned i;

    for (i = 0U; i < VFD_WORK_MAX; i++) {
        unsigned enc = (*cursor + i) % VFD_WORK_MAX;
        if (work_is_due(views, enc, want_fast, now_ms)) {
            *cursor   = (enc + 1U) % VFD_WORK_MAX;
            *out_enc  = enc;
            return true;
        }
    }
    return false;
}

static void vfd_pulse_tick(void)
{
    uint64_t now_ms = time_util_get_ms();
    unsigned i;

    for (i = 0U; i < HAL_VFD_MANAGER_SLOT_MAX; i++) {
        if (!s_slot[i].bound || !s_slot[i].initialized) {
            continue;
        }
        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        pulse_out_tick(&s_slot[i].rst_pulse, now_ms);
        pthread_mutex_unlock(&s_vfd_lock);
    }
}

static void vfd_monitor_tick(void)
{
    vfd_slot_view_t views[HAL_VFD_MANAGER_SLOT_MAX];
    uint64_t        now_ms = time_util_get_ms();
    unsigned        enc    = 0U;
    unsigned        slot;
    unsigned        ch;
    bool            is_fast;
    bool            ok;
    uint64_t        last_ms;
    uint32_t        period_ms;

    take_slot_views(views);

    is_fast = false;
    if (s_fast_since_keepalive >= HAL_VFD_FAST_KEEPALIVE_EVERY) {
        if (pick_due_work(views, &s_bg_rr, false, now_ms, &enc)) {
            s_fast_since_keepalive = 0U;
        } else if (pick_due_work(views, &s_fast_rr, true, now_ms, &enc)) {
            is_fast = true;
        } else {
            return;
        }
    } else if (pick_due_work(views, &s_fast_rr, true, now_ms, &enc)) {
        is_fast = true;
    } else if (pick_due_work(views, &s_bg_rr, false, now_ms, &enc)) {
        is_fast = false;
    } else {
        return;
    }

    slot = enc / VFD_CH_COUNT;
    ch   = enc % VFD_CH_COUNT;
    if (ch == VFD_CH_FAULT) {
        last_ms   = views[slot].last_fault_ms;
        period_ms = views[slot].fault.period_ms;
        ok        = monitor_sample_fault(&s_slot[slot], now_ms);
    } else {
        last_ms   = views[slot].last_current_ms;
        period_ms = views[slot].current.period_ms;
        ok        = monitor_sample_current(&s_slot[slot], now_ms);
    }

    if (is_fast) {
        if (s_fast_since_keepalive < 0xFFU) {
            s_fast_since_keepalive++;
        }
        if (ok) {
            note_fast_sample(last_ms, now_ms, period_ms);
        }
    }
}

static void vfd_pulse_task(void *ctx)
{
    (void)ctx;
    vfd_pulse_tick();
}

static void vfd_monitor_task(void *ctx)
{
    (void)ctx;
    vfd_monitor_tick();
}

sw_err_t hal_vfd_manager_poll_register_task(void)
{
    sw_err_t ret;

    ret = periodic_task_register(
        "vfd_pulse_poll", HAL_VFD_PULSE_PERIOD_MS, vfd_pulse_task, NULL, SCHED_OTHER, 0, THD_VFD_TICK_STACK);
    if (ret != SW_OK) {
        return ret;
    }
    return periodic_task_register(
        "vfd_monitor_poll", HAL_VFD_MONITOR_SLICE_MS, vfd_monitor_task, NULL, SCHED_OTHER, 0, THD_VFD_TICK_STACK);
}

static sw_err_t vfd_set_gear(hal_vfd_id_t id, hal_vfd_gear_t gear)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);
    sw_err_t        ret;

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }

    if (gear == 0) {
        return vfd_stop(id);
    }
    vfd_mutexes_ready();
    pthread_mutex_lock(&s_control_lock);
    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    if (slot->control_mode == VFD_CONTROL_FREQUENCY) {
        pthread_mutex_unlock(&s_vfd_lock);
        pthread_mutex_unlock(&s_control_lock);
        return SW_ERR_STATE;
    }
    pthread_mutex_unlock(&s_vfd_lock);
    ret = slot->cfg.ops->apply_gear(slot->cfg.drv_ctx, gear);
    if (ret == SW_OK) {
        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        slot->control_mode = VFD_CONTROL_GEAR;
        pthread_mutex_unlock(&s_vfd_lock);
    }
    pthread_mutex_unlock(&s_control_lock);
    return ret;
}

static sw_err_t vfd_set_frequency(hal_vfd_id_t id, hal_vfd_frequency_t frequency_centi_hz)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);
    sw_err_t        ret;

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (frequency_centi_hz == 0) {
        return vfd_stop(id);
    }
    vfd_mutexes_ready();
    pthread_mutex_lock(&s_control_lock);
    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    if (slot->control_mode == VFD_CONTROL_GEAR) {
        pthread_mutex_unlock(&s_vfd_lock);
        pthread_mutex_unlock(&s_control_lock);
        return SW_ERR_STATE;
    }
    pthread_mutex_unlock(&s_vfd_lock);
    ret = slot->cfg.ops->apply_frequency(slot->cfg.drv_ctx, frequency_centi_hz);
    if (ret == SW_OK) {
        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        slot->control_mode = VFD_CONTROL_FREQUENCY;
        pthread_mutex_unlock(&s_vfd_lock);
    }
    pthread_mutex_unlock(&s_control_lock);
    return ret;
}

static sw_err_t vfd_stop(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);
    sw_err_t        ret;

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }
    vfd_mutexes_ready();
    pthread_mutex_lock(&s_control_lock);
    ret = slot->cfg.ops->stop_outputs(slot->cfg.drv_ctx);

    if (ret == SW_OK) {
        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        slot->control_mode = VFD_CONTROL_NONE;
        pthread_mutex_unlock(&s_vfd_lock);
    }
    pthread_mutex_unlock(&s_control_lock);
    return ret;
}

static sw_err_t vfd_fault_reset(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);
    sw_err_t        ret;
    bool            use_io;
    uint64_t        now_ms;

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }

    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    pulse_out_cancel(&slot->rst_pulse);
    pthread_mutex_unlock(&s_vfd_lock);

    ret = vfd_stop(id);
    if (ret != SW_OK) {
        return ret;
    }

    use_io = (slot->cfg.ops->has_rst_pin != NULL) && slot->cfg.ops->has_rst_pin(slot->cfg.drv_ctx);
    if (use_io) {
        now_ms = time_util_get_ms();
        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        ret = pulse_out_start(&slot->rst_pulse, slot->cfg.rst_pulse_ms, now_ms);
        pthread_mutex_unlock(&s_vfd_lock);
        return ret;
    }

    if ((slot->cfg.ops->clear_fault == NULL) || !slot->cfg.ops->has_clear_fault(slot->cfg.drv_ctx)) {
        LOG_ERROR("hal_vfd: fault_reset id=%d no rst pin and no modbus clear", id);
        return SW_ERR_PARAM;
    }
    return slot->cfg.ops->clear_fault(slot->cfg.drv_ctx);
}

static hal_vfd_state_t vfd_get_state(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);

    if (slot == NULL) {
        return HAL_VFD_STATE_STOPPED;
    }
    return slot->cfg.ops->get_state(slot->cfg.drv_ctx);
}

static sw_err_t vfd_read(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (p_val == NULL) {
        return SW_ERR_PARAM;
    }

    return slot->cfg.ops->read(slot->cfg.drv_ctx, reg, p_val);
}

static sw_err_t vfd_get_cached(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);
    sw_err_t        ret  = SW_OK;

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (p_val == NULL) {
        return SW_ERR_PARAM;
    }

    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    switch (reg) {
    case HAL_VFD_REG_FAULT_CODE:
        *p_val = slot->cached_fault_code;
        break;
    case HAL_VFD_REG_CURRENT:
        *p_val = slot->cached_current;
        break;
    default:
        ret = SW_ERR_PARAM;
        break;
    }
    pthread_mutex_unlock(&s_vfd_lock);
    return ret;
}

static void vfd_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code))
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);

    if (slot != NULL) {
        vfd_mutexes_ready();
        pthread_mutex_lock(&s_vfd_lock);
        slot->event_cb = cb;
        pthread_mutex_unlock(&s_vfd_lock);
    }
}

static const hal_vfd_ops_t s_ops = {
    .init              = vfd_init,
    .set_gear          = vfd_set_gear,
    .set_frequency     = vfd_set_frequency,
    .stop              = vfd_stop,
    .fault_reset       = vfd_fault_reset,
    .get_state         = vfd_get_state,
    .read              = vfd_read,
    .get_cached        = vfd_get_cached,
    .register_event_cb = vfd_register_event_cb,
};

void hal_vfd_manager_register(void)
{
    hal_vfd_register(&s_ops);
}

#ifdef HAL_VFD_MANAGER_UNIT_TEST
void hal_vfd_manager_test_pulse_tick(void)
{
    vfd_pulse_tick();
}

void hal_vfd_manager_test_monitor_tick(void)
{
    vfd_monitor_tick();
}

void hal_vfd_manager_test_tick(void)
{
    vfd_pulse_tick();
    vfd_monitor_tick();
}

void hal_vfd_manager_test_reset(void)
{
    vfd_mutexes_ready();
    pthread_mutex_lock(&s_vfd_lock);
    memset(s_slot, 0, sizeof(s_slot));
    pthread_mutex_unlock(&s_vfd_lock);
    s_fast_rr              = 0U;
    s_bg_rr                = 0U;
    s_fast_since_keepalive = 0U;
    s_fast_lag_warn_ms     = 0U;
}
#endif
