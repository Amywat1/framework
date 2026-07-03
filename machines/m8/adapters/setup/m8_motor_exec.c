/**
 * @file    m8_motor_exec.c
 * @brief   M8 机型共享电机执行器实现。
 *
 * 将龙门（VFD）、侧刷/顶刷（共用 VFD，接触器切换）、升降（继电器）、
 * 后轮锁止（继电器）、风机（VFD）六路电机统一配置进同一 motor_executor_t，
 * motor_tick 只需调用一次即可推进全部轴。限位传感器通过共享 sensors.limit
 * 回调按电机索引分派。
 */

#include "machines/m8/adapters/setup/m8_motor_exec.h"
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
#include <pthread.h>
#include <unistd.h>

/** 共享执行器管理的电机/驱动器总数（龙门、侧刷、顶刷、升降、后轮锁止、风机） */
#define M8_MOTOR_COUNT  6U

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
 * 时间源（全局单一时钟，提前定义供接触器切换 prepare 回调使用）
 * ========================================================================= */

static uint64_t m8_now_ms(void *ctx)
{
    struct timespec ts;
    (void)ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
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
 * 驱动器 1/2：侧刷/顶刷 VFD（仅正转，无高速 DO，共用一台物理 VFD，
 * 靠接触器切换选中对象——两个驱动器实例的 set_output/cutoff/is_running/
 * current 完全相同，只有 prepare 不同）
 * ========================================================================= */

/** 接触器切换的输出对象编号（与物理 VFD 电机身份无关，仅供接触器切换状态机使用） */
#define BRUSH_CONTACTOR_SIDE  0
#define BRUSH_CONTACTOR_TOP   1

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

/* -------------------- 接触器切换：仅 prepare 不同的部分 --------------------
 * 两阶段非阻塞切换时序：断电旧接触器（等 CFG_BRUSH_CONTACTOR_RELEASE_MS）→
 * 通电新接触器（等 CFG_BRUSH_CONTACTOR_CLOSE_MS）→ 完成。与 MCC 在
 * WAITING_START 阶段逐 tick 轮询 prepare() 的契约对应，只比较时间戳，不阻塞。
 * ------------------------------------------------------------------------- */

typedef enum {
    BRUSH_CONTACTOR_PHASE_IDLE,      /* 默认值 0：上电默认状态 */
    BRUSH_CONTACTOR_PHASE_RELEASING,
    BRUSH_CONTACTOR_PHASE_ENGAGING,
} brush_contactor_phase_t;

/* 零初始化即等价于 phase=IDLE、current_id=BRUSH_CONTACTOR_SIDE，无需显式 init */
static brush_contactor_phase_t s_contactor_phase;
static int                     s_contactor_current_id;
static uint64_t                s_contactor_until_ms;

static void brush_contactor_set(int output_id, bool on)
{
    (void)io_do_set((output_id == BRUSH_CONTACTOR_SIDE) ? M8_IO_DO_SIDE_BRUSH_ACT
                                                         : M8_IO_DO_TOP_BRUSH_ACT,
                     on);
}

static bool brush_contactor_prepare(int target_id)
{
    uint64_t now_ms = m8_now_ms(NULL);

    switch (s_contactor_phase) {
    case BRUSH_CONTACTOR_PHASE_IDLE:
        if (s_contactor_current_id == target_id) {
            return true;
        }
        brush_contactor_set(s_contactor_current_id, false);
        s_contactor_phase    = BRUSH_CONTACTOR_PHASE_RELEASING;
        s_contactor_until_ms = now_ms + CFG_BRUSH_CONTACTOR_RELEASE_MS;
        return false;

    case BRUSH_CONTACTOR_PHASE_RELEASING:
        if (now_ms < s_contactor_until_ms) {
            return false;
        }
        brush_contactor_set(target_id, true);
        s_contactor_current_id = target_id;
        s_contactor_phase      = BRUSH_CONTACTOR_PHASE_ENGAGING;
        s_contactor_until_ms   = now_ms + CFG_BRUSH_CONTACTOR_CLOSE_MS;
        return false;

    case BRUSH_CONTACTOR_PHASE_ENGAGING:
        if (target_id != s_contactor_current_id) {
            /* 吸合等待期间目标又变更，需重新切换 */
            brush_contactor_set(s_contactor_current_id, false);
            s_contactor_phase    = BRUSH_CONTACTOR_PHASE_RELEASING;
            s_contactor_until_ms = now_ms + CFG_BRUSH_CONTACTOR_RELEASE_MS;
            return false;
        }
        if (now_ms < s_contactor_until_ms) {
            return false;
        }
        s_contactor_phase = BRUSH_CONTACTOR_PHASE_IDLE;
        return true;

    default:
        return false;
    }
}

static bool brush_side_prepare(void *ctx)
{
    (void)ctx;
    return brush_contactor_prepare(BRUSH_CONTACTOR_SIDE);
}

static bool brush_top_prepare(void *ctx)
{
    (void)ctx;
    return brush_contactor_prepare(BRUSH_CONTACTOR_TOP);
}

/* =========================================================================
 * 驱动器 5：风机 VFD（仅正转，无高速 DO，与龙门/刷子共用 485 总线）
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
 * 驱动器 3：升降继电器
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
 * 驱动器 4：后轮锁止继电器
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

    case M8_MOTOR_BRUSH_SIDE:
    case M8_MOTOR_BRUSH_TOP:
        /* 侧刷/顶刷电机无硬限位 */
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
    NULL,              /* M8_MOTOR_BRUSH_SIDE */
    NULL,              /* M8_MOTOR_BRUSH_TOP */
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

static motor_driver_t s_drv_brush_side = {
    .set_output = brush_set_output,
    .cutoff     = brush_cutoff,
    .reset      = brush_reset,
    .prepare    = brush_side_prepare,
    .is_running = brush_is_running,
    .current    = brush_current,
    .ctx        = NULL,
};

static motor_driver_t s_drv_brush_top = {
    .set_output = brush_set_output,
    .cutoff     = brush_cutoff,
    .reset      = brush_reset,
    .prepare    = brush_top_prepare,
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
    &s_drv_gantry,     /* driver 0 → M8_MOTOR_GANTRY */
    &s_drv_brush_side, /* driver 1 → M8_MOTOR_BRUSH_SIDE */
    &s_drv_brush_top,  /* driver 2 → M8_MOTOR_BRUSH_TOP */
    &s_drv_lift,       /* driver 3 → M8_MOTOR_LIFT */
    &s_drv_rear_lock,  /* driver 4 → M8_MOTOR_REAR_LOCK */
    &s_drv_fan,        /* driver 5 → M8_MOTOR_FAN */
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
 * tick 后台线程：按 20ms 节拍推进共享执行器
 * ========================================================================= */

/** 每次 tick 循环的休眠间隔（ms） */
#define MOTOR_TICK_INTERVAL_MS  20U

/** tick 线程栈大小 */
#define MOTOR_TICK_STACK_SIZE   (16U * 1024U)

static void *m8_motor_tick_thread_fn(void *arg)
{
    (void)arg;
    for (;;) {
        motor_tick(&s_exec);
        usleep((unsigned long)MOTOR_TICK_INTERVAL_MS * 1000UL);
    }
    return NULL;
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
    cfg.tick_ms         = CFG_GANTRY_TICK_MS;   /* 所有电机共用 20ms 节拍 */
    cfg.watchdog_ms     = CFG_GANTRY_WATCHDOG_MS;

    /* 侧刷/顶刷共用一台 VFD，双向互斥：任一方运行相关态时另一方不可启动 */
    cfg.interlocks[0].kind = MOTOR_INTERLOCK_MUTEX;
    cfg.interlocks[0].a    = M8_MOTOR_BRUSH_SIDE;
    cfg.interlocks[0].b    = M8_MOTOR_BRUSH_TOP;
    cfg.interlocks[1].kind = MOTOR_INTERLOCK_MUTEX;
    cfg.interlocks[1].a    = M8_MOTOR_BRUSH_TOP;
    cfg.interlocks[1].b    = M8_MOTOR_BRUSH_SIDE;
    cfg.interlock_count    = 2;

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

    /* --- 电机 1/2：侧刷/顶刷 VFD（共用一台物理驱动器，靠接触器切换，
     *     prep_required 触发 prepare() 完成切换后才允许进入 RUNNING）--- */
    m = &cfg.motors[M8_MOTOR_BRUSH_SIDE];
    m->driver_index        = M8_MOTOR_BRUSH_SIDE;
    m->has_encoder         = false;
    m->cap_position_move   = false;
    m->prep_required       = true;
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

    m = &cfg.motors[M8_MOTOR_BRUSH_TOP];
    *m = cfg.motors[M8_MOTOR_BRUSH_SIDE];
    m->driver_index         = M8_MOTOR_BRUSH_TOP;

    /* --- 电机 3：升降继电器 --- */
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

    /* --- 电机 4：后轮锁止继电器 --- */
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

    /* --- 电机 5：风机 VFD --- */
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

    LOG_INFO("m8_motor_exec_init ok (6 motors)");
    return SW_OK;
}

sw_err_t m8_motor_exec_start(void)
{
    pthread_attr_t attr;
    pthread_t      tid;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MOTOR_TICK_STACK_SIZE);

    if (pthread_create(&tid, &attr, m8_motor_tick_thread_fn, NULL) != 0) {
        pthread_attr_destroy(&attr);
        LOG_ERROR("m8_motor_exec_start: pthread_create 失败");
        return SW_ERR_HW;
    }

    pthread_attr_destroy(&attr);
    pthread_detach(tid);
    LOG_INFO("m8_motor_exec_start: tick 线程已启动，间隔 %ums", MOTOR_TICK_INTERVAL_MS);
    return SW_OK;
}
