/**
 * @file    hal_vfd_manager.c
 * @brief   VFD HAL 通用组合层实现
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"

#include "common/log.h"
#include "common/pulse_out.h"
#include "common/time_util.h"
#include "ports/outbound/hal/hal_vfd_port.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <pthread.h>
#include <sched.h>
#include <string.h>

/** @brief 连续 Modbus 失败 N 次后上报 COMM_LOST */
#define VFD_COMM_FAIL_NOTIFY           3U
#define HAL_VFD_MANAGER_POLL_PERIOD_MS 20U

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
    uint64_t last_monitor_ms;

    pulse_out_slot_t rst_pulse;
} hal_vfd_slot_t;

static hal_vfd_slot_t s_slot[HAL_VFD_MANAGER_SLOT_MAX];

/** @brief 保护上面除 backend 调用以外的全部运行时簿记字段 */
static pthread_mutex_t s_vfd_lock = PTHREAD_MUTEX_INITIALIZER;

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

static bool bind_cfg_valid(const hal_vfd_manager_bind_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->ops == NULL) || (cfg->drv_ctx == NULL)) {
        return false;
    }
    if ((cfg->ops->apply_gear == NULL) || (cfg->ops->stop_outputs == NULL) || (cfg->ops->set_rst == NULL)
        || (cfg->ops->read == NULL) || (cfg->ops->write == NULL) || (cfg->ops->get_state == NULL)) {
        return false;
    }
    if ((cfg->rst_pulse_ms == 0U) || (cfg->monitor_period_ms == 0U)) {
        return false;
    }
    return true;
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

static void monitor_sample(hal_vfd_slot_t *slot, uint64_t now_ms)
{
    hal_vfd_state_t        st;
    sw_err_t               ret;
    uint16_t               val = 0U;
    hal_vfd_monitor_mask_t mask;
    bool                   comm_restored;
    bool                   comm_lost;
    int                    fault_evt;

    pthread_mutex_lock(&s_vfd_lock);
    mask = slot->cfg.monitor_mask;
    pthread_mutex_unlock(&s_vfd_lock);

    if ((mask & HAL_VFD_MON_FAULT) != 0U) {
        ret = slot->cfg.ops->read(slot->cfg.drv_ctx, HAL_VFD_REG_FAULT_CODE, &val);

        comm_restored = false;
        comm_lost     = false;
        fault_evt     = 0;

        pthread_mutex_lock(&s_vfd_lock);
        if (ret == SW_OK) {
            comm_restored = on_comm_success_locked(slot);
            fault_evt     = update_fault_state_locked(slot, val);
        } else if (ret == SW_ERR_COMM) {
            comm_lost = on_comm_failure_locked(slot);
        }
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
    }

    if ((mask & HAL_VFD_MON_CURRENT) != 0U) {
        st = slot->cfg.ops->get_state(slot->cfg.drv_ctx);
        if ((st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV)) {
            ret = slot->cfg.ops->read(slot->cfg.drv_ctx, HAL_VFD_REG_CURRENT, &val);

            comm_restored = false;
            comm_lost     = false;

            pthread_mutex_lock(&s_vfd_lock);
            if (ret == SW_OK) {
                comm_restored        = on_comm_success_locked(slot);
                slot->cached_current = val;
            } else if (ret == SW_ERR_COMM) {
                comm_lost = on_comm_failure_locked(slot);
            }
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
        }
    }

    pthread_mutex_lock(&s_vfd_lock);
    slot->last_monitor_ms = now_ms;
    pthread_mutex_unlock(&s_vfd_lock);
}

sw_err_t hal_vfd_manager_bind(hal_vfd_id_t id, const hal_vfd_manager_bind_cfg_t *cfg)
{
    hal_vfd_slot_t *slot;

    if (!slot_id_valid(id) || !bind_cfg_valid(cfg)) {
        return SW_ERR_PARAM;
    }

    slot = &s_slot[(unsigned)id];

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
    slot->last_monitor_ms     = 0U;
    slot->rst_pulse.ctx       = slot;
    slot->rst_pulse.set_level = pulse_set_rst_level;
    slot->rst_pulse.active    = false;
    pthread_mutex_unlock(&s_vfd_lock);
    return SW_OK;
}

sw_err_t hal_vfd_manager_set_monitor_mask(hal_vfd_id_t id, hal_vfd_monitor_mask_t mask)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }

    pthread_mutex_lock(&s_vfd_lock);
    slot->cfg.monitor_mask = mask;
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

        pthread_mutex_lock(&s_vfd_lock);
        pulse_out_cancel(&s_slot[i].rst_pulse);
        s_slot[i].cached_fault_code = 0U;
        s_slot[i].cached_current    = 0U;
        s_slot[i].fault_active      = false;
        s_slot[i].comm_ok           = true;
        s_slot[i].comm_fail_count   = 0U;
        s_slot[i].last_monitor_ms   = 0U;
        s_slot[i].initialized       = (ret == SW_OK);
        pthread_mutex_unlock(&s_vfd_lock);

        if ((ret != SW_OK) && (first_ret == SW_OK)) {
            first_ret = ret;
        }
    }
    return first_ret;
}

static void vfd_tick(void)
{
    uint64_t now_ms = time_util_get_ms();
    unsigned i;

    for (i = 0U; i < HAL_VFD_MANAGER_SLOT_MAX; i++) {
        hal_vfd_slot_t        *slot = &s_slot[i];
        hal_vfd_monitor_mask_t mask;
        bool                   due;

        if (!slot->bound || !slot->initialized) {
            continue;
        }

        pthread_mutex_lock(&s_vfd_lock);
        pulse_out_tick(&slot->rst_pulse, now_ms);
        mask = slot->cfg.monitor_mask;
        due  = (mask != HAL_VFD_MON_NONE)
              && (time_elapsed_ms(slot->last_monitor_ms, now_ms) >= slot->cfg.monitor_period_ms);
        pthread_mutex_unlock(&s_vfd_lock);

        if (due) {
            monitor_sample(slot, now_ms);
        }
    }
}

static void vfd_poll_task(void *ctx)
{
    (void)ctx;
    vfd_tick();
}

sw_err_t hal_vfd_manager_poll_register_task(void)
{
    return periodic_task_register(
        "vfd_manager_poll", HAL_VFD_MANAGER_POLL_PERIOD_MS, vfd_poll_task, NULL, SCHED_OTHER, 0, THD_VFD_TICK_STACK);
}

static sw_err_t vfd_run(hal_vfd_id_t id, hal_vfd_gear_t gear)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }

    if (gear == 0) {
        return slot->cfg.ops->stop_outputs(slot->cfg.drv_ctx);
    }
    return slot->cfg.ops->apply_gear(slot->cfg.drv_ctx, gear);
}

static sw_err_t vfd_set_freq(hal_vfd_id_t id, uint16_t freq_hz)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return slot->cfg.ops->write(slot->cfg.drv_ctx, HAL_VFD_REG_FREQ, freq_hz);
}

static sw_err_t vfd_stop(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = ready_slot_by_id(id);

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return slot->cfg.ops->stop_outputs(slot->cfg.drv_ctx);
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

    pthread_mutex_lock(&s_vfd_lock);
    pulse_out_cancel(&slot->rst_pulse);
    pthread_mutex_unlock(&s_vfd_lock);

    ret = slot->cfg.ops->stop_outputs(slot->cfg.drv_ctx);
    if (ret != SW_OK) {
        return ret;
    }

    use_io = (slot->cfg.ops->has_rst_pin != NULL) && slot->cfg.ops->has_rst_pin(slot->cfg.drv_ctx);
    if (use_io) {
        now_ms = time_util_get_ms();
        pthread_mutex_lock(&s_vfd_lock);
        ret = pulse_out_start(&slot->rst_pulse, slot->cfg.rst_pulse_ms, now_ms);
        pthread_mutex_unlock(&s_vfd_lock);
        return ret;
    }

    ret = slot->cfg.ops->write(slot->cfg.drv_ctx, HAL_VFD_REG_CLEAR_FAULT, 0U);
    if (ret == SW_ERR_PARAM) {
        LOG_ERROR("hal_vfd: fault_reset id=%d no rst pin and no modbus clear", id);
    }
    return ret;
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
        pthread_mutex_lock(&s_vfd_lock);
        slot->event_cb = cb;
        pthread_mutex_unlock(&s_vfd_lock);
    }
}

static const hal_vfd_ops_t s_ops = {
    .init              = vfd_init,
    .run               = vfd_run,
    .set_freq          = vfd_set_freq,
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
void hal_vfd_manager_test_tick(void)
{
    vfd_tick();
}

void hal_vfd_manager_test_reset(void)
{
    pthread_mutex_lock(&s_vfd_lock);
    memset(s_slot, 0, sizeof(s_slot));
    pthread_mutex_unlock(&s_vfd_lock);
}
#endif
