/**
 * @file    m8_motor_exec.c
 * @brief   M8 机型共享电机执行器实现。
 *
 * 将龙门（VFD）、刷子（VFD）、升降（继电器）、后轮锁止（继电器）、风机（VFD）
 * 五路电机统一配置进同一 motor_executor_t，motor_tick 只需调用一次即可推进全部轴。
 * 限位传感器通过共享 sensors.limit 回调按电机索引分派。
 */

#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "machines/m8/adapters/setup/m8_motor_tick.h"
#include "machines/m8/config/m8_gantry_config.h"
#include "machines/m8/config/m8_brush_config.h"
#include "machines/m8/config/m8_lift_config.h"
#include "machines/m8/config/m8_rear_lock_config.h"
#include "machines/m8/config/m8_fan_config.h"
#include "machines/m8/config/m8_io_pins.h"
#include "machines/m8/config/m8_vfd_table.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/hal/hal_io_port.h"
#include "common/vfd_types.h"
#include "common/log.h"
#include <stddef.h>
#include <string.h>
#include <time.h>

/** 共享执行器管理的电机/驱动器总数（龙门、刷子、升降、后轮锁止、风机） */
#define M8_MOTOR_COUNT  5U

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

static uint16_t centi_hz_to_hz(int freq_centi_hz)
{
    return (uint16_t)((freq_centi_hz + 99) / 100);
}

/* =========================================================================
 * VFD 驱动共享实现：龙门与刷子的差异仅为 VFD 号、是否有高速 DO、是否可反转，
 * 具体到每台电机的函数只负责把这些差异参数传给下面几个共享函数。
 * ========================================================================= */

static void vfd_drive(hal_vfd_id_t id, io_do_t high_speed_do, int high_speed_threshold_hz,
                       bool reversible, int freq_centi_hz, motor_direction_t dir)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if (vfd == NULL) {
        return;
    }

    if (freq_centi_hz > 0) {
        uint16_t freq_hz = centi_hz_to_hz(freq_centi_hz);
        if (io_do_raw(high_speed_do) != IO_HANDLE_NULL) {
            (void)io_do_set(high_speed_do, freq_hz >= (uint16_t)high_speed_threshold_hz);
        }
        (void)vfd->set_freq(id, freq_hz);
        (void)vfd->run(id, (reversible && dir == MOTOR_DIR_REVERSE)
                           ? (hal_vfd_gear_t)(-1) : (hal_vfd_gear_t)1);
    } else {
        if (io_do_raw(high_speed_do) != IO_HANDLE_NULL) {
            (void)io_do_set(high_speed_do, false);
        }
        (void)vfd->stop(id);
    }
}

static void vfd_cutoff_id(hal_vfd_id_t id, io_do_t high_speed_do)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if (io_do_raw(high_speed_do) != IO_HANDLE_NULL) {
        (void)io_do_set(high_speed_do, false);
    }
    if (vfd != NULL) {
        (void)vfd->stop(id);
    }
}

static bool vfd_reset_id(hal_vfd_id_t id)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if (vfd == NULL) {
        return false;
    }
    (void)vfd->fault_reset(id);
    return true;
}

static bool vfd_is_running_id(hal_vfd_id_t id)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    hal_vfd_state_t st;

    if (vfd == NULL) {
        return false;
    }
    st = vfd->get_state(id);
    return (st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV);
}

static int vfd_current_id(hal_vfd_id_t id)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t val = 0;

    if (vfd == NULL) {
        return 0;
    }
    (void)vfd->get_cached(id, HAL_VFD_REG_CURRENT, &val);
    return (int)val;
}

/* =========================================================================
 * 驱动器 0：龙门 VFD（支持正反转 + 高速 DO 切换）
 * ========================================================================= */

static void gantry_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    (void)ctx;
    vfd_drive(HAL_VFD_GANTRY, M8_IO_DO_GANTRY_HIGH_SPEED,
              CFG_GANTRY_HIGH_SPEED_THRESHOLD_HZ, true, freq_centi_hz, dir);
}

static void gantry_cutoff(void *ctx)
{
    (void)ctx;
    vfd_cutoff_id(HAL_VFD_GANTRY, M8_IO_DO_GANTRY_HIGH_SPEED);
}

static bool gantry_reset(void *ctx)
{
    (void)ctx;
    return vfd_reset_id(HAL_VFD_GANTRY);
}

static bool gantry_is_running(void *ctx)
{
    (void)ctx;
    return vfd_is_running_id(HAL_VFD_GANTRY);
}

static int gantry_current(void *ctx)
{
    (void)ctx;
    return vfd_current_id(HAL_VFD_GANTRY);
}

/* =========================================================================
 * 驱动器 1：刷子 VFD（仅正转，无高速 DO）
 * ========================================================================= */

static void brush_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    (void)ctx;
    vfd_drive(HAL_VFD_BRUSH, (io_do_t){IO_HANDLE_NULL}, 0, false, freq_centi_hz, dir);
}

static void brush_cutoff(void *ctx)
{
    (void)ctx;
    vfd_cutoff_id(HAL_VFD_BRUSH, (io_do_t){IO_HANDLE_NULL});
}

static bool brush_reset(void *ctx)
{
    (void)ctx;
    return vfd_reset_id(HAL_VFD_BRUSH);
}

static bool brush_is_running(void *ctx)
{
    (void)ctx;
    return vfd_is_running_id(HAL_VFD_BRUSH);
}

static int brush_current(void *ctx)
{
    (void)ctx;
    return vfd_current_id(HAL_VFD_BRUSH);
}

/* =========================================================================
 * 驱动器 4：风机 VFD（仅正转，无高速 DO，与龙门/刷子共用 485 总线）
 * ========================================================================= */

static void fan_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    (void)ctx;
    vfd_drive(HAL_VFD_FAN, (io_do_t){IO_HANDLE_NULL}, 0, false, freq_centi_hz, dir);
}

static void fan_cutoff(void *ctx)
{
    (void)ctx;
    vfd_cutoff_id(HAL_VFD_FAN, (io_do_t){IO_HANDLE_NULL});
}

static bool fan_reset(void *ctx)
{
    (void)ctx;
    return vfd_reset_id(HAL_VFD_FAN);
}

static bool fan_is_running(void *ctx)
{
    (void)ctx;
    return vfd_is_running_id(HAL_VFD_FAN);
}

static int fan_current(void *ctx)
{
    (void)ctx;
    return vfd_current_id(HAL_VFD_FAN);
}

/* =========================================================================
 * 继电器驱动共享实现：升降与后轮锁止的差异仅为两路 DO 引脚，
 * 具体到每台电机的函数只负责把各自的引脚和运行状态变量传给下面两个共享函数。
 * ========================================================================= */

static void relay_drive(io_do_t pin_fwd, io_do_t pin_bwd, int freq_centi_hz,
                         motor_direction_t dir, bool *running)
{
    if (freq_centi_hz > 0) {
        (void)io_do_set(pin_fwd, dir == MOTOR_DIR_FORWARD);
        (void)io_do_set(pin_bwd, dir != MOTOR_DIR_FORWARD);
        *running = true;
    } else {
        (void)io_do_set(pin_fwd, false);
        (void)io_do_set(pin_bwd, false);
        *running = false;
    }
}

static void relay_cutoff_pins(io_do_t pin_fwd, io_do_t pin_bwd, bool *running)
{
    (void)io_do_set(pin_fwd, false);
    (void)io_do_set(pin_bwd, false);
    *running = false;
}

/* =========================================================================
 * 驱动器 2：升降继电器
 * ========================================================================= */

static bool s_lift_running = false;

static void lift_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    (void)ctx;
    relay_drive(M8_IO_DO_TOP_BRUSH_UP, M8_IO_DO_TOP_BRUSH_DOWN, freq_centi_hz, dir, &s_lift_running);
}

static void lift_cutoff(void *ctx)
{
    (void)ctx;
    relay_cutoff_pins(M8_IO_DO_TOP_BRUSH_UP, M8_IO_DO_TOP_BRUSH_DOWN, &s_lift_running);
}

static bool lift_relay_reset(void *ctx)
{
    (void)ctx;
    relay_cutoff_pins(M8_IO_DO_TOP_BRUSH_UP, M8_IO_DO_TOP_BRUSH_DOWN, &s_lift_running);
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
    relay_drive(M8_IO_DO_ROD_EXTEND, M8_IO_DO_ROD_RETRACT, freq_centi_hz, dir, &s_rear_lock_running);
}

static void rear_lock_cutoff(void *ctx)
{
    (void)ctx;
    relay_cutoff_pins(M8_IO_DO_ROD_EXTEND, M8_IO_DO_ROD_RETRACT, &s_rear_lock_running);
}

static bool rear_lock_relay_reset(void *ctx)
{
    (void)ctx;
    relay_cutoff_pins(M8_IO_DO_ROD_EXTEND, M8_IO_DO_ROD_RETRACT, &s_rear_lock_running);
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

    case M8_MOTOR_FAN:
        /* 风机电机无硬限位 */
        return false;

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
static motor_encoder_t * const s_encoders[M8_MOTOR_COUNT] = {
    &s_gantry_encoder, /* M8_MOTOR_GANTRY */
    NULL,              /* M8_MOTOR_BRUSH */
    NULL,              /* M8_MOTOR_LIFT */
    NULL,              /* M8_MOTOR_REAR_LOCK */
    NULL,              /* M8_MOTOR_FAN */
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

static motor_driver_t s_drv_fan = {
    .set_output = fan_set_output,
    .cutoff     = fan_cutoff,
    .reset      = fan_reset,
    .prepare    = NULL,
    .is_running = fan_is_running,
    .current    = fan_current,
    .ctx        = NULL,
};

static motor_driver_t * const s_drivers[M8_MOTOR_COUNT] = {
    &s_drv_gantry,    /* driver 0 → M8_MOTOR_GANTRY */
    &s_drv_brush,     /* driver 1 → M8_MOTOR_BRUSH */
    &s_drv_lift,      /* driver 2 → M8_MOTOR_LIFT */
    &s_drv_rear_lock, /* driver 3 → M8_MOTOR_REAR_LOCK */
    &s_drv_fan,       /* driver 4 → M8_MOTOR_FAN */
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
    cfg.motor_count     = M8_MOTOR_COUNT;
    cfg.driver_count    = M8_MOTOR_COUNT;
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

    /* --- 电机 4：风机 VFD --- */
    m = &cfg.motors[M8_MOTOR_FAN];
    m->driver_index        = M8_MOTOR_FAN;
    m->has_encoder          = false;
    m->cap_position_move    = false;
    m->cooldown_ms          = CFG_FAN_COOLDOWN_MS;
    m->reversal_stop_ms     = CFG_FAN_REVERSAL_STOP_MS;
    m->accel_ms             = CFG_FAN_ACCEL_MS;
    m->decel_ms             = CFG_FAN_DECEL_MS;
    m->default_max_move_ms  = CFG_FAN_DEFAULT_MAX_MOVE_MS;
    m->slow_freq            = CFG_FAN_GEAR_FREQ;
    m->gear_freq[0]         = CFG_FAN_GEAR_FREQ;
    m->gear_count           = CFG_FAN_GEAR_COUNT;
    m->mon.monitor_current  = false;
    m->mon.monitor_feedback = false;

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

    LOG_INFO("m8_motor_exec_init ok (5 motors, 1 tick)");
    return SW_OK;
}
