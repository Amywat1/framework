/**
 * @file    m8_motor_exec.c
 * @brief   M8 机型共享电机执行器实现。
 *
 * 将龙门（VFD）、刷子（VFD）、升降（继电器）、后轮锁止（继电器）四路电机
 * 统一配置进同一 motor_executor_t，motor_tick 只需调用一次即可推进全部轴。
 * 限位传感器通过共享 sensors.limit 回调按电机索引分派。
 */

#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "machines/m8/adapters/setup/m8_motor_tick.h"
#include "machines/m8/config/m8_gantry_config.h"
#include "machines/m8/config/m8_brush_config.h"
#include "machines/m8/config/m8_lift_config.h"
#include "machines/m8/config/m8_rear_lock_config.h"
#include "machines/m8/config/m8_io_pins.h"
#include "machines/m8/config/m8_vfd_table.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/hal/hal_io_port.h"
#include "common/vfd_types.h"
#include "common/log.h"
#include <stddef.h>
#include <string.h>
#include <time.h>

/* =========================================================================
 * 辅助：DO 写
 * ========================================================================= */

static sw_err_t io_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

/* =========================================================================
 * 驱动器 0：龙门 VFD
 * ========================================================================= */

static void gantry_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t freq_hz;
    hal_vfd_gear_t gear;

    (void)ctx;
    if (vfd == NULL) {
        return;
    }

    if (freq_centi_hz > 0) {
        freq_hz = (uint16_t)((freq_centi_hz + 99) / 100);
        (void)io_do_set(M8_IO_DO_GANTRY_HIGH_SPEED,
                        freq_hz >= CFG_GANTRY_HIGH_SPEED_THRESHOLD_HZ);
        (void)vfd->set_freq(HAL_VFD_GANTRY, freq_hz);
        gear = (dir == MOTOR_DIR_FORWARD) ? (hal_vfd_gear_t)1 : (hal_vfd_gear_t)(-1);
        (void)vfd->run(HAL_VFD_GANTRY, gear);
    } else {
        (void)io_do_set(M8_IO_DO_GANTRY_HIGH_SPEED, false);
        (void)vfd->stop(HAL_VFD_GANTRY);
    }
}

static void gantry_cutoff(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    (void)io_do_set(M8_IO_DO_GANTRY_HIGH_SPEED, false);
    if (vfd != NULL) {
        (void)vfd->stop(HAL_VFD_GANTRY);
    }
}

static bool gantry_reset(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    (void)vfd->fault_reset(HAL_VFD_GANTRY);
    return true;
}

static bool gantry_is_running(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    hal_vfd_state_t st = vfd->get_state(HAL_VFD_GANTRY);
    return (st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV);
}

static int gantry_current(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t val = 0;

    (void)ctx;
    if (vfd == NULL) {
        return 0;
    }
    (void)vfd->get_cached(HAL_VFD_GANTRY, HAL_VFD_REG_CURRENT, &val);
    return (int)val;
}

/* =========================================================================
 * 驱动器 1：刷子 VFD
 * ========================================================================= */

static void brush_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    (void)dir;  /* 刷子 VFD 无反转引脚，始终正转 */
    if (vfd == NULL) {
        return;
    }

    if (freq_centi_hz > 0) {
        uint16_t freq_hz = (uint16_t)((freq_centi_hz + 99) / 100);
        (void)vfd->set_freq(HAL_VFD_BRUSH, freq_hz);
        (void)vfd->run(HAL_VFD_BRUSH, 1);
    } else {
        (void)vfd->stop(HAL_VFD_BRUSH);
    }
}

static void brush_cutoff(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd != NULL) {
        (void)vfd->stop(HAL_VFD_BRUSH);
    }
}

static bool brush_reset(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    (void)vfd->fault_reset(HAL_VFD_BRUSH);
    return true;
}

static bool brush_is_running(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    hal_vfd_state_t st = vfd->get_state(HAL_VFD_BRUSH);
    return (st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV);
}

static int brush_current(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t val = 0;

    (void)ctx;
    if (vfd == NULL) {
        return 0;
    }
    (void)vfd->get_cached(HAL_VFD_BRUSH, HAL_VFD_REG_CURRENT, &val);
    return (int)val;
}

/* =========================================================================
 * 驱动器 2：升降继电器
 * ========================================================================= */

static bool s_lift_running = false;

static void lift_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    (void)ctx;
    if (freq_centi_hz > 0) {
        if (dir == MOTOR_DIR_FORWARD) {
            (void)io_do_set(M8_IO_DO_TOP_BRUSH_UP,   true);
            (void)io_do_set(M8_IO_DO_TOP_BRUSH_DOWN, false);
        } else {
            (void)io_do_set(M8_IO_DO_TOP_BRUSH_UP,   false);
            (void)io_do_set(M8_IO_DO_TOP_BRUSH_DOWN, true);
        }
        s_lift_running = true;
    } else {
        (void)io_do_set(M8_IO_DO_TOP_BRUSH_UP,   false);
        (void)io_do_set(M8_IO_DO_TOP_BRUSH_DOWN, false);
        s_lift_running = false;
    }
}

static void lift_cutoff(void *ctx)
{
    (void)ctx;
    (void)io_do_set(M8_IO_DO_TOP_BRUSH_UP,   false);
    (void)io_do_set(M8_IO_DO_TOP_BRUSH_DOWN, false);
    s_lift_running = false;
}

static bool lift_relay_reset(void *ctx)
{
    (void)ctx;
    (void)io_do_set(M8_IO_DO_TOP_BRUSH_UP,   false);
    (void)io_do_set(M8_IO_DO_TOP_BRUSH_DOWN, false);
    s_lift_running = false;
    return true;
}

static bool lift_is_running(void *ctx)
{
    (void)ctx;
    return s_lift_running;
}

/* =========================================================================
 * 驱动器 3：后轮锁止继电器
 * ========================================================================= */

static bool s_rear_lock_running = false;

static void rear_lock_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    (void)ctx;
    if (freq_centi_hz > 0) {
        if (dir == MOTOR_DIR_FORWARD) {
            (void)io_do_set(M8_IO_DO_ROD_EXTEND,  true);
            (void)io_do_set(M8_IO_DO_ROD_RETRACT, false);
        } else {
            (void)io_do_set(M8_IO_DO_ROD_EXTEND,  false);
            (void)io_do_set(M8_IO_DO_ROD_RETRACT, true);
        }
        s_rear_lock_running = true;
    } else {
        (void)io_do_set(M8_IO_DO_ROD_EXTEND,  false);
        (void)io_do_set(M8_IO_DO_ROD_RETRACT, false);
        s_rear_lock_running = false;
    }
}

static void rear_lock_cutoff(void *ctx)
{
    (void)ctx;
    (void)io_do_set(M8_IO_DO_ROD_EXTEND,  false);
    (void)io_do_set(M8_IO_DO_ROD_RETRACT, false);
    s_rear_lock_running = false;
}

static bool rear_lock_relay_reset(void *ctx)
{
    (void)ctx;
    (void)io_do_set(M8_IO_DO_ROD_EXTEND,  false);
    (void)io_do_set(M8_IO_DO_ROD_RETRACT, false);
    s_rear_lock_running = false;
    return true;
}

static bool rear_lock_is_running(void *ctx)
{
    (void)ctx;
    return s_rear_lock_running;
}

/* =========================================================================
 * 编码器：仅龙门有脉冲编码器
 * ========================================================================= */

static int64_t gantry_enc_raw(void *ctx)
{
    const hal_io_ops_t *io = hal_io_get_ops();
    int val;

    (void)ctx;
    if ((io == NULL) || (io->pulse_read == NULL)) {
        return 0;
    }
    val = io->pulse_read(M8_IO_DI_GANTRY_ENCODER_PULSE);
    return (val < 0) ? 0 : (int64_t)val;
}

static bool gantry_enc_zero(void *ctx)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    (void)ctx;
    if ((io == NULL) || (io->pulse_clear == NULL)) {
        return false;
    }
    return io->pulse_clear(M8_IO_DI_GANTRY_ENCODER_PULSE) == SW_OK;
}

/* =========================================================================
 * 共享限位传感器：按电机索引分派
 * ========================================================================= */

static bool m8_all_limits(void *ctx, int motor, motor_limit_kind_t kind)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    (void)ctx;
    if ((io == NULL) || (io->di_read == NULL)) {
        return false;
    }

    switch (motor) {
    case M8_MOTOR_GANTRY:
        switch (kind) {
        case MOTOR_LIMIT_POS:    return io->di_read(M8_IO_DI_GANTRY_FWD_LIMIT);
        case MOTOR_LIMIT_NEG:
        case MOTOR_LIMIT_ORIGIN: return io->di_read(M8_IO_DI_GANTRY_REV_LIMIT);
        default:                 return false;
        }

    case M8_MOTOR_BRUSH:
        /* 刷子电机无硬限位 */
        return false;

    case M8_MOTOR_LIFT:
        switch (kind) {
        case MOTOR_LIMIT_POS:    return io->di_read(M8_IO_DI_LIFT_UP_LIMIT);
        case MOTOR_LIMIT_NEG:
        case MOTOR_LIMIT_ORIGIN: return io->di_read(M8_IO_DI_LIFT_DOWN_LIMIT);
        default:                 return false;
        }

    case M8_MOTOR_REAR_LOCK:
        switch (kind) {
        case MOTOR_LIMIT_POS:    return io->di_read(M8_IO_DI_REAR_WHEEL_LOCK);
        case MOTOR_LIMIT_NEG:
        case MOTOR_LIMIT_ORIGIN: return io->di_read(M8_IO_DI_REAR_LOCK_HOME);
        default:                 return false;
        }

    default:
        return false;
    }
}

/* =========================================================================
 * 急停（由上层安全模块统一处理）
 * ========================================================================= */

static bool m8_estop_active(void *ctx)
{
    (void)ctx;
    return false;
}

/* =========================================================================
 * 时间源（全局单一时钟）
 * ========================================================================= */

static uint64_t m8_now_ms(void *ctx)
{
    struct timespec ts;
    (void)ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

/* =========================================================================
 * 静态 MCC 对象
 * ========================================================================= */

static motor_executor_t s_exec;

static motor_encoder_t s_gantry_encoder = {
    .raw  = gantry_enc_raw,
    .zero = gantry_enc_zero,
    .ctx  = NULL,
};

/* 编码器数组：长度必须等于电机数，无编码器的电机填 NULL */
static motor_encoder_t * const s_encoders[4] = {
    &s_gantry_encoder, /* M8_MOTOR_GANTRY */
    NULL,              /* M8_MOTOR_BRUSH */
    NULL,              /* M8_MOTOR_LIFT */
    NULL,              /* M8_MOTOR_REAR_LOCK */
};

static motor_driver_t s_drv_gantry = {
    .set_output = gantry_set_output,
    .cutoff     = gantry_cutoff,
    .reset      = gantry_reset,
    .prepare    = NULL,
    .is_running = gantry_is_running,
    .current    = gantry_current,
    .ctx        = NULL,
};

static motor_driver_t s_drv_brush = {
    .set_output = brush_set_output,
    .cutoff     = brush_cutoff,
    .reset      = brush_reset,
    .prepare    = NULL,
    .is_running = brush_is_running,
    .current    = brush_current,
    .ctx        = NULL,
};

static motor_driver_t s_drv_lift = {
    .set_output = lift_set_output,
    .cutoff     = lift_cutoff,
    .reset      = lift_relay_reset,
    .prepare    = NULL,
    .is_running = lift_is_running,
    .current    = NULL,
    .ctx        = NULL,
};

static motor_driver_t s_drv_rear_lock = {
    .set_output = rear_lock_set_output,
    .cutoff     = rear_lock_cutoff,
    .reset      = rear_lock_relay_reset,
    .prepare    = NULL,
    .is_running = rear_lock_is_running,
    .current    = NULL,
    .ctx        = NULL,
};

static motor_driver_t * const s_drivers[4] = {
    &s_drv_gantry,    /* driver 0 → M8_MOTOR_GANTRY */
    &s_drv_brush,     /* driver 1 → M8_MOTOR_BRUSH */
    &s_drv_lift,      /* driver 2 → M8_MOTOR_LIFT */
    &s_drv_rear_lock, /* driver 3 → M8_MOTOR_REAR_LOCK */
};

static motor_clock_t s_clock = {
    .now_ms = m8_now_ms,
    .ctx    = NULL,
};

static motor_sensors_t s_sensors = {
    .limit = m8_all_limits,
    .ctx   = NULL,
};

static motor_estop_t s_estop = {
    .active = m8_estop_active,
    .ctx    = NULL,
};

/* =========================================================================
 * tick：m8_motor_exec_tick 注册后，由 tick 线程每 20ms 调用一次
 * ========================================================================= */

static void m8_motor_exec_tick(void)
{
    motor_tick(&s_exec);
}

/* =========================================================================
 * 公共函数
 * ========================================================================= */

motor_executor_t *m8_motor_exec_get(void)
{
    return s_exec.initialized ? &s_exec : NULL;
}

sw_err_t m8_motor_exec_init(void)
{
    sw_err_t ret;
    motor_config_t cfg;
    motor_motor_cfg_t *m;
    motor_ports_t ports;
    motor_init_result_t init_res;

    if (hal_vfd_get_ops() == NULL) {
        LOG_ERROR("m8_motor_exec_init: hal_vfd not registered");
        return SW_ERR_NOT_INIT;
    }
    if (hal_io_get_ops() == NULL) {
        LOG_ERROR("m8_motor_exec_init: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.motor_count     = 4;
    cfg.driver_count    = 4;
    cfg.interlock_count = 0;
    cfg.tick_ms         = CFG_GANTRY_TICK_MS;   /* 所有电机共用 20ms 节拍 */
    cfg.watchdog_ms     = CFG_GANTRY_WATCHDOG_MS;

    /* --- 电机 0：龙门行走 VFD --- */
    m = &cfg.motors[M8_MOTOR_GANTRY];
    m->driver_index        = M8_MOTOR_GANTRY;
    m->has_encoder         = true;
    m->cap_position_move   = true;
    m->cooldown_ms         = CFG_GANTRY_COOLDOWN_MS;
    m->reversal_stop_ms    = CFG_GANTRY_REVERSAL_STOP_MS;
    m->accel_ms            = CFG_GANTRY_ACCEL_MS;
    m->decel_ms            = CFG_GANTRY_DECEL_MS;
    m->default_max_move_ms = CFG_GANTRY_DEFAULT_MAX_MOVE_MS;
    m->enc_stall_ticks     = CFG_GANTRY_ENC_STALL_TICKS;
    m->enc_jump_max        = CFG_GANTRY_ENC_JUMP_MAX;
    m->enc_escalate        = false;
    m->slow_freq           = CFG_GANTRY_GEAR_FREQ_LOW;
    m->gear_freq[0]        = CFG_GANTRY_GEAR_FREQ_LOW;
    m->gear_freq[1]        = CFG_GANTRY_GEAR_FREQ_HIGH;
    m->gear_count          = CFG_GANTRY_GEAR_COUNT;
    m->mon.monitor_current  = false;
    m->mon.monitor_feedback = false;

    /* --- 电机 1：刷子 VFD（侧刷/顶刷共用接触器切换）--- */
    m = &cfg.motors[M8_MOTOR_BRUSH];
    m->driver_index        = M8_MOTOR_BRUSH;
    m->has_encoder         = false;
    m->cap_position_move   = false;
    m->cooldown_ms         = CFG_BRUSH_COOLDOWN_MS;
    m->reversal_stop_ms    = 0;
    m->accel_ms            = CFG_BRUSH_ACCEL_MS;
    m->decel_ms            = CFG_BRUSH_DECEL_MS;
    m->default_max_move_ms = CFG_BRUSH_DEFAULT_MAX_MOVE_MS;
    m->slow_freq           = CFG_BRUSH_GEAR_FREQ_LOW;
    m->gear_freq[0]        = CFG_BRUSH_GEAR_FREQ_LOW;
    m->gear_freq[1]        = CFG_BRUSH_GEAR_FREQ_NORMAL;
    m->gear_freq[2]        = CFG_BRUSH_GEAR_FREQ_HIGH;
    m->gear_count          = CFG_BRUSH_GEAR_COUNT;
    m->mon.monitor_current    = true;
    m->mon.cur_max_steady     = CFG_BRUSH_CUR_MAX_STEADY;
    m->mon.cur_min_steady     = CFG_BRUSH_CUR_MIN_STEADY;
    m->mon.cur_max_accel      = CFG_BRUSH_CUR_MAX_STEADY;
    m->mon.cur_min_accel      = 0;
    m->mon.cur_confirm_ms     = CFG_BRUSH_CUR_CONFIRM_MS;
    m->mon.startup_delay_ms   = CFG_BRUSH_CUR_STARTUP_DELAY_MS;
    m->mon.monitor_feedback   = false;

    /* --- 电机 2：升降继电器 --- */
    m = &cfg.motors[M8_MOTOR_LIFT];
    m->driver_index        = M8_MOTOR_LIFT;
    m->has_encoder         = false;
    m->cap_position_move   = false;
    m->cooldown_ms         = CFG_LIFT_COOLDOWN_MS;
    m->reversal_stop_ms    = CFG_LIFT_REVERSAL_STOP_MS;
    m->accel_ms            = CFG_LIFT_ACCEL_MS;
    m->decel_ms            = CFG_LIFT_DECEL_MS;
    m->default_max_move_ms = CFG_LIFT_DEFAULT_MAX_MOVE_MS;
    m->slow_freq           = CFG_LIFT_GEAR_FREQ;
    m->gear_freq[0]        = CFG_LIFT_GEAR_FREQ;
    m->gear_count          = CFG_LIFT_GEAR_COUNT;

    /* --- 电机 3：后轮锁止继电器 --- */
    m = &cfg.motors[M8_MOTOR_REAR_LOCK];
    m->driver_index        = M8_MOTOR_REAR_LOCK;
    m->has_encoder         = false;
    m->cap_position_move   = false;
    m->cooldown_ms         = CFG_REAR_LOCK_COOLDOWN_MS;
    m->reversal_stop_ms    = CFG_REAR_LOCK_REVERSAL_STOP_MS;
    m->accel_ms            = CFG_REAR_LOCK_ACCEL_MS;
    m->decel_ms            = CFG_REAR_LOCK_DECEL_MS;
    m->default_max_move_ms = CFG_REAR_LOCK_DEFAULT_MAX_MOVE_MS;
    m->slow_freq           = CFG_REAR_LOCK_GEAR_FREQ;
    m->gear_freq[0]        = CFG_REAR_LOCK_GEAR_FREQ;
    m->gear_count          = CFG_REAR_LOCK_GEAR_COUNT;

    /* --- 端口集合 --- */
    memset(&ports, 0, sizeof(ports));
    ports.clock    = &s_clock;
    ports.drivers  = s_drivers;
    ports.encoders = s_encoders;
    ports.sensors  = &s_sensors;
    ports.estop    = &s_estop;

    init_res = motor_init(&s_exec, &cfg, &ports);
    if (!init_res.ok) {
        LOG_ERROR("m8_motor_exec_init: motor_init failed: %s", init_res.error);
        return SW_ERR_HW;
    }

    /* 注册唯一的电机 tick：每 20ms 推进全部 4 轴 */
    ret = m8_motor_tick_register(m8_motor_exec_tick);
    if (ret != SW_OK) {
        LOG_ERROR("m8_motor_exec_init: m8_motor_tick_register 失败 ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_motor_exec_init ok (4 motors, 1 tick)");
    return SW_OK;
}
