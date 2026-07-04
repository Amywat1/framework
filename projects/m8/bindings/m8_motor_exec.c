/**
 * @file    m8_motor_exec.c
 * @brief   M8 机型共享电机执行器实现。
 *
 * 将龙门（VFD）、侧刷/顶刷（共用 VFD，接触器切换）、升降（继电器）、
 * 后轮锁止（继电器）、风机（VFD）六路电机统一配置进同一 motor_executor_t，
 * motor_tick 只需调用一次即可推进全部轴。限位传感器通过共享 sensors.limit
 * 回调按电机索引分派。
 */

#include "projects/m8/bindings/m8_motor_exec.h"

#include "framework/common/log.h"
#include "framework/common/time_util.h"
#include "framework/common/vfd_types.h"
#include "projects/m8/config/m8_brush_config.h"
#include "projects/m8/config/m8_fan_config.h"
#include "projects/m8/config/m8_gantry_config.h"
#include "projects/m8/config/m8_io_pins.h"
#include "projects/m8/config/m8_lift_config.h"
#include "projects/m8/config/m8_rear_lock_config.h"
#include "projects/m8/config/m8_vfd_table.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/ports/outbound/hal/hal_vfd_port.h"

#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

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

/**
 * @brief  将 MCC 输出的厘赫值映射为 VFD 挡位号（1-based）
 * @param  freq_centi_hz  MCC set_output 给定值（挡位频率或加减速斜坡中间值）
 * @param  gear_freq      挡位→厘赫映射表
 * @param  gear_count     有效挡位数
 * @retval 挡位号 1..gear_count；无效输入返回 0
 */
static int centi_hz_to_gear(int freq_centi_hz, const int *gear_freq, int gear_count)
{
    int i;
    int best = 0;

    if ((freq_centi_hz <= 0) || (gear_freq == NULL) || (gear_count <= 0)) {
        return 0;
    }

    for (i = 0; i < gear_count; i++) {
        if (gear_freq[i] == freq_centi_hz) {
            return i + 1;
        }
        if (gear_freq[i] <= freq_centi_hz) {
            best = i + 1;
        }
    }
    return (best > 0) ? best : 1;
}

/**
 * @brief  将 time_util_get_ms 适配为 motor_clock_t.now_ms 回调签名
 * @param  ctx  透传上下文，当前未使用
 * @retval 当前毫秒时间戳
 */
static uint64_t motor_exec_now_ms(void *ctx)
{
    (void)ctx;
    return (uint64_t)time_util_get_ms();
}

/* =========================================================================
 * VFD 驱动上下文与通用实现
 *
 * 所有 VFD 电机（龙门、刷子、风机）共用同一套函数指针，
 * 由各自的 vfd_ctx_t 携带差异参数（VFD 号、挡位表、高速 DO、可否反转）。
 * MCC 回调 set_output 的入参为厘赫（接口约定，不可改）；仅在 generic_vfd_set_output
 * 入口做一次挡位映射，内部 VFD 控制只传挡位，不调用 set_freq。
 * ========================================================================= */

/** @brief VFD 驱动器端口上下文，描述单台 VFD 的硬件参数。 */
typedef struct {
    hal_vfd_id_t vfd_id;                     /**< VFD 枚举编号 */
    io_do_t      high_speed_do;              /**< 高速挡 DO（无则填 IO_HANDLE_NULL） */
    int          high_speed_gear;            /**< 绝对挡位 >= 此值时拉高 high_speed_do，0=不联动 */
    bool         reversible;                 /**< 是否支持反转 */
    int          gear_freq[MOTOR_MAX_GEARS]; /**< MCC 挡位→厘赫映射表（与 motor_cfg 一致） */
    int          gear_count;                 /**< 有效挡位数 */
} vfd_ctx_t;

/** @brief 无刷子接触器切换时的 brush_contactor_id 占位值。 */
#define M8_BRUSH_CONTACTOR_NONE (-1)

/**
 * @brief VFD 电机驱动器上下文（含可选刷子接触器切换编号）。
 *
 * 龙门/风机仅使用 vfd 字段；侧刷/顶刷额外通过 brush_contactor_id
 * 驱动 prepare 回调完成接触器切换。
 */
typedef struct {
    vfd_ctx_t vfd;                /**< VFD 硬件参数 */
    int       brush_contactor_id; /**< 接触器编号，M8_BRUSH_CONTACTOR_NONE 表示无 */
} m8_vfd_driver_ctx_t;

/**
 * @brief  按挡位驱动 VFD
 * @param  ctx       VFD 上下文
 * @param  abs_gear  绝对挡位 1..N，0 表示停止
 * @param  dir       运动方向（reversible 为 true 时反转取负挡位）
 */
static void vfd_set_gear(const vfd_ctx_t *ctx, int abs_gear, motor_direction_t dir)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    hal_vfd_gear_t       gear;

    if ((vfd == NULL) || (ctx == NULL)) {
        return;
    }

    if (abs_gear > 0) {
        if (ctx->reversible && (dir == MOTOR_DIR_REVERSE)) {
            gear = (hal_vfd_gear_t)(-abs_gear);
        } else {
            gear = (hal_vfd_gear_t)abs_gear;
        }
        if (io_do_raw(ctx->high_speed_do) != IO_HANDLE_NULL) {
            bool high = (ctx->high_speed_gear > 0) && (abs_gear >= ctx->high_speed_gear);
            (void)io_do_set(ctx->high_speed_do, high);
        }
        (void)vfd->run(ctx->vfd_id, gear);
    } else {
        if (io_do_raw(ctx->high_speed_do) != IO_HANDLE_NULL) {
            (void)io_do_set(ctx->high_speed_do, false);
        }
        (void)vfd->stop(ctx->vfd_id);
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
    return vfd->fault_reset(id) == SW_OK;
}

static bool vfd_is_running_id(hal_vfd_id_t id)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    hal_vfd_state_t      st;

    if (vfd == NULL) {
        return false;
    }
    st = vfd->get_state(id);
    return (st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV);
}

static int vfd_current_id(hal_vfd_id_t id)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t             val = 0;

    if (vfd == NULL) {
        return 0;
    }
    (void)vfd->get_cached(id, HAL_VFD_REG_CURRENT, &val);
    return (int)val;
}

/**
 * @brief  MCC set_output 回调：将厘赫映射为挡位后输出（MCC 接口固定传 freq，不可改）
 */
static void generic_vfd_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    const m8_vfd_driver_ctx_t *m = ctx;
    int                        abs_gear;

    if (freq_centi_hz <= 0) {
        vfd_set_gear(&m->vfd, 0, dir);
        return;
    }

    abs_gear = centi_hz_to_gear(freq_centi_hz, m->vfd.gear_freq, m->vfd.gear_count);
    vfd_set_gear(&m->vfd, abs_gear, dir);
}

static void generic_vfd_cutoff(void *ctx)
{
    const m8_vfd_driver_ctx_t *m = ctx;
    vfd_cutoff_id(m->vfd.vfd_id, m->vfd.high_speed_do);
}

static bool generic_vfd_reset(void *ctx)
{
    const m8_vfd_driver_ctx_t *m = ctx;
    return vfd_reset_id(m->vfd.vfd_id);
}

static bool generic_vfd_is_running(void *ctx)
{
    const m8_vfd_driver_ctx_t *m = ctx;
    return vfd_is_running_id(m->vfd.vfd_id);
}

static int generic_vfd_current(void *ctx)
{
    const m8_vfd_driver_ctx_t *m = ctx;
    return vfd_current_id(m->vfd.vfd_id);
}

/* =========================================================================
 * 刷子接触器切换：仅 prepare 不同的部分
 *
 * 两阶段非阻塞切换时序：断电旧接触器（等 CFG_BRUSH_CONTACTOR_RELEASE_MS）→
 * 通电新接触器（等 CFG_BRUSH_CONTACTOR_CLOSE_MS）→ 完成。与 MCC 在
 * WAITING_START 阶段逐 tick 轮询 prepare() 的契约对应，用 time_elapsed_ms()
 * 计算等待时长，不阻塞。
 * ========================================================================= */

/** 接触器切换的输出对象编号（与物理 VFD 电机身份无关，仅供接触器切换状态机使用） */
#define BRUSH_CONTACTOR_SIDE 0
#define BRUSH_CONTACTOR_TOP  1

typedef enum {
    BRUSH_CONTACTOR_PHASE_IDLE, /* 默认值 0：上电默认状态 */
    BRUSH_CONTACTOR_PHASE_RELEASING,
    BRUSH_CONTACTOR_PHASE_ENGAGING,
} brush_contactor_phase_t;

/* 零初始化即等价于 phase=IDLE、current_id=BRUSH_CONTACTOR_SIDE，无需显式 init */
static brush_contactor_phase_t s_contactor_phase;
static int                     s_contactor_current_id;
static uint32_t                s_contactor_start_ms;

static void brush_contactor_set(int output_id, bool on)
{
    (void)io_do_set((output_id == BRUSH_CONTACTOR_SIDE) ? M8_IO_DO_SIDE_BRUSH_ACT : M8_IO_DO_TOP_BRUSH_ACT, on);
}

static bool brush_contactor_prepare(int target_id)
{
    uint32_t now_ms = time_util_get_ms();

    switch (s_contactor_phase) {
    case BRUSH_CONTACTOR_PHASE_IDLE:
        if (s_contactor_current_id == target_id) {
            return true;
        }
        brush_contactor_set(s_contactor_current_id, false);
        s_contactor_phase    = BRUSH_CONTACTOR_PHASE_RELEASING;
        s_contactor_start_ms = now_ms;
        return false;

    case BRUSH_CONTACTOR_PHASE_RELEASING:
        if (time_elapsed_ms(s_contactor_start_ms, now_ms) < CFG_BRUSH_CONTACTOR_RELEASE_MS) {
            return false;
        }
        brush_contactor_set(target_id, true);
        s_contactor_current_id = target_id;
        s_contactor_phase      = BRUSH_CONTACTOR_PHASE_ENGAGING;
        s_contactor_start_ms   = now_ms;
        return false;

    case BRUSH_CONTACTOR_PHASE_ENGAGING:
        if (target_id != s_contactor_current_id) {
            /* 吸合等待期间目标又变更，需重新切换 */
            brush_contactor_set(s_contactor_current_id, false);
            s_contactor_phase    = BRUSH_CONTACTOR_PHASE_RELEASING;
            s_contactor_start_ms = now_ms;
            return false;
        }
        if (time_elapsed_ms(s_contactor_start_ms, now_ms) < CFG_BRUSH_CONTACTOR_CLOSE_MS) {
            return false;
        }
        s_contactor_phase = BRUSH_CONTACTOR_PHASE_IDLE;
        return true;

    default:
        return false;
    }
}

static bool brush_contactor_driver_prepare(void *ctx)
{
    const m8_vfd_driver_ctx_t *m = ctx;
    return brush_contactor_prepare(m->brush_contactor_id);
}

/* =========================================================================
 * 继电器驱动上下文与通用实现
 *
 * 升降与后轮锁止共用同一套函数指针，
 * 由各自的 relay_ctx_t 携带差异参数（正反转 DO 引脚、运行状态指针）。
 * ========================================================================= */

/** @brief 继电器驱动器端口上下文，描述单路继电器的引脚与运行状态。 */
typedef struct {
    io_do_t pin_fwd; /**< 正转 DO */
    io_do_t pin_bwd; /**< 反转 DO */
    bool   *running; /**< 运行状态标志，由继电器函数维护 */
} relay_ctx_t;

static void relay_drive(io_do_t pin_fwd, io_do_t pin_bwd, int freq_centi_hz, motor_direction_t dir, bool *running)
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

static void generic_relay_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    relay_ctx_t *s = ctx;
    relay_drive(s->pin_fwd, s->pin_bwd, freq_centi_hz, dir, s->running);
}

static void generic_relay_cutoff(void *ctx)
{
    relay_ctx_t *s = ctx;
    relay_cutoff_pins(s->pin_fwd, s->pin_bwd, s->running);
}

static bool generic_relay_reset(void *ctx)
{
    relay_ctx_t *s = ctx;
    relay_cutoff_pins(s->pin_fwd, s->pin_bwd, s->running);
    return true;
}

static bool generic_relay_is_running(void *ctx)
{
    const relay_ctx_t *s = ctx;
    return *s->running;
}

/* =========================================================================
 * 编码器：仅龙门有脉冲编码器
 * ========================================================================= */

static int64_t gantry_enc_raw(void *ctx)
{
    const hal_io_ops_t *io = hal_io_get_ops();
    int                 val;

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
 * 共享限位传感器：按电机索引查表
 * ========================================================================= */

/** @brief 单台电机的限位 DI 映射（NEG 与 ORIGIN 共用 neg 引脚）。 */
typedef struct {
    io_di_t pos; /**< 正向端限位，IO_HANDLE_NULL 表示无 */
    io_di_t neg; /**< 反向端/原点限位，IO_HANDLE_NULL 表示无 */
} m8_limit_map_t;

static const m8_limit_map_t s_limit_map[M8_MOTOR_COUNT] = {
    /*                        pos                           neg */
    [M8_MOTOR_GANTRY]    = {M8_IO_DI_GANTRY_FWD_LIMIT, M8_IO_DI_GANTRY_REV_LIMIT},
    [M8_MOTOR_LIFT]      = {M8_IO_DI_LIFT_UP_LIMIT,    M8_IO_DI_LIFT_DOWN_LIMIT },
    [M8_MOTOR_REAR_LOCK] = {M8_IO_DI_REAR_WHEEL_LOCK,  M8_IO_DI_REAR_LOCK_HOME  },
};

static bool m8_all_limits(void *ctx, int motor, motor_limit_kind_t kind)
{
    const hal_io_ops_t   *io = hal_io_get_ops();
    const m8_limit_map_t *map;
    io_di_t               pin;

    (void)ctx;
    if ((io == NULL) || (io->di_read == NULL)) {
        return false;
    }
    if ((motor < 0) || (motor >= M8_MOTOR_COUNT)) {
        return false;
    }

    map = &s_limit_map[motor];
    switch (kind) {
    case MOTOR_LIMIT_POS:
        pin = map->pos;
        break;
    case MOTOR_LIMIT_NEG:
    case MOTOR_LIMIT_ORIGIN:
        pin = map->neg;
        break;
    default:
        return false;
    }

    if (io_di_raw(pin) == IO_HANDLE_NULL) {
        return false;
    }
    return io->di_read(pin);
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
 * 静态 ctx 实例（内容在 m8_motor_exec_init 中填充）
 * ========================================================================= */

static m8_vfd_driver_ctx_t s_gantry_drv_ctx;
static m8_vfd_driver_ctx_t s_brush_side_drv_ctx;
static m8_vfd_driver_ctx_t s_brush_top_drv_ctx;
static m8_vfd_driver_ctx_t s_fan_drv_ctx;

static bool        s_lift_running      = false;
static bool        s_rear_lock_running = false;
static relay_ctx_t s_lift_relay_ctx;
static relay_ctx_t s_rear_lock_relay_ctx;

/* =========================================================================
 * 静态 MCC 对象
 * ========================================================================= */

static motor_executor_t s_exec;

static motor_encoder_t s_gantry_encoder = {
    .raw  = gantry_enc_raw,
    .zero = gantry_enc_zero,
    .ctx  = NULL,
};

/* 编码器数组：仅列出有编码器的电机，其余自动为 NULL */
static motor_encoder_t *const s_encoders[M8_MOTOR_COUNT] = {
    [M8_MOTOR_GANTRY] = &s_gantry_encoder,
};

#define M8_DEFINE_VFD_DRIVER(var_name, prep_fn, drv_ctx_ptr)                                                           \
    static motor_driver_t var_name = {                                                                                 \
        .set_output = generic_vfd_set_output,                                                                          \
        .cutoff     = generic_vfd_cutoff,                                                                              \
        .reset      = generic_vfd_reset,                                                                               \
        .prepare    = prep_fn,                                                                                         \
        .is_running = generic_vfd_is_running,                                                                          \
        .current    = generic_vfd_current,                                                                             \
        .ctx        = drv_ctx_ptr,                                                                                     \
    }

#define M8_DEFINE_RELAY_DRIVER(var_name, relay_ctx_ptr)                                                                \
    static motor_driver_t var_name = {                                                                                 \
        .set_output = generic_relay_set_output,                                                                        \
        .cutoff     = generic_relay_cutoff,                                                                            \
        .reset      = generic_relay_reset,                                                                             \
        .prepare    = NULL,                                                                                            \
        .is_running = generic_relay_is_running,                                                                        \
        .current    = NULL,                                                                                            \
        .ctx        = relay_ctx_ptr,                                                                                   \
    }

M8_DEFINE_VFD_DRIVER(s_drv_gantry, NULL, &s_gantry_drv_ctx);
M8_DEFINE_VFD_DRIVER(s_drv_brush_side, brush_contactor_driver_prepare, &s_brush_side_drv_ctx);
M8_DEFINE_VFD_DRIVER(s_drv_brush_top, brush_contactor_driver_prepare, &s_brush_top_drv_ctx);
M8_DEFINE_RELAY_DRIVER(s_drv_lift, &s_lift_relay_ctx);
M8_DEFINE_RELAY_DRIVER(s_drv_rear_lock, &s_rear_lock_relay_ctx);
M8_DEFINE_VFD_DRIVER(s_drv_fan, NULL, &s_fan_drv_ctx);

static motor_driver_t *const s_drivers[M8_MOTOR_COUNT] = {
    [M8_MOTOR_GANTRY]     = &s_drv_gantry,
    [M8_MOTOR_BRUSH_SIDE] = &s_drv_brush_side,
    [M8_MOTOR_BRUSH_TOP]  = &s_drv_brush_top,
    [M8_MOTOR_LIFT]       = &s_drv_lift,
    [M8_MOTOR_REAR_LOCK]  = &s_drv_rear_lock,
    [M8_MOTOR_FAN]        = &s_drv_fan,
};

static motor_clock_t s_clock = {
    .now_ms = motor_exec_now_ms,
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
#define MOTOR_TICK_INTERVAL_MS 20U

/** tick 线程栈大小 */
#define MOTOR_TICK_STACK_SIZE (16U * 1024U)

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
    motor_config_t      cfg;
    motor_motor_cfg_t  *m;
    motor_ports_t       ports;
    motor_init_result_t init_res;

    if (hal_vfd_get_ops() == NULL) {
        LOG_ERROR("m8_motor_exec_init: hal_vfd not registered");
        return SW_ERR_NOT_INIT;
    }
    if (hal_io_get_ops() == NULL) {
        LOG_ERROR("m8_motor_exec_init: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    /* 初始化驱动器上下文（io_do_t 为结构体，不可用于静态初始化器） */
    s_gantry_drv_ctx = (m8_vfd_driver_ctx_t){
        .vfd = {
            .vfd_id          = HAL_VFD_GANTRY,
            .high_speed_do   = M8_IO_DO_GANTRY_HIGH_SPEED,
            .high_speed_gear = CFG_GANTRY_GEAR_COUNT,
            .reversible      = true,
            .gear_freq       = {CFG_GANTRY_GEAR_FREQ_LOW, CFG_GANTRY_GEAR_FREQ_HIGH},
            .gear_count      = CFG_GANTRY_GEAR_COUNT,
        },
        .brush_contactor_id = M8_BRUSH_CONTACTOR_NONE,
    };
    s_brush_side_drv_ctx = (m8_vfd_driver_ctx_t){
        .vfd = {
            .vfd_id          = HAL_VFD_BRUSH,
            .high_speed_do   = (io_do_t){IO_HANDLE_NULL},
            .high_speed_gear = 0,
            .reversible      = false,
            .gear_freq       = {CFG_BRUSH_GEAR_FREQ_LOW, CFG_BRUSH_GEAR_FREQ_NORMAL, CFG_BRUSH_GEAR_FREQ_HIGH},
            .gear_count      = CFG_BRUSH_GEAR_COUNT,
        },
        .brush_contactor_id = BRUSH_CONTACTOR_SIDE,
    };
    s_brush_top_drv_ctx = (m8_vfd_driver_ctx_t){
        .vfd = {
            .vfd_id          = HAL_VFD_BRUSH,
            .high_speed_do   = (io_do_t){IO_HANDLE_NULL},
            .high_speed_gear = 0,
            .reversible      = false,
            .gear_freq       = {CFG_BRUSH_GEAR_FREQ_LOW, CFG_BRUSH_GEAR_FREQ_NORMAL, CFG_BRUSH_GEAR_FREQ_HIGH},
            .gear_count      = CFG_BRUSH_GEAR_COUNT,
        },
        .brush_contactor_id = BRUSH_CONTACTOR_TOP,
    };
    s_fan_drv_ctx = (m8_vfd_driver_ctx_t){
        .vfd = {
            .vfd_id          = HAL_VFD_FAN,
            .high_speed_do   = (io_do_t){IO_HANDLE_NULL},
            .high_speed_gear = 0,
            .reversible      = false,
            .gear_freq       = {CFG_FAN_GEAR_FREQ},
            .gear_count      = CFG_FAN_GEAR_COUNT,
        },
        .brush_contactor_id = M8_BRUSH_CONTACTOR_NONE,
    };
    s_lift_relay_ctx = (relay_ctx_t){
        .pin_fwd = M8_IO_DO_TOP_BRUSH_UP,
        .pin_bwd = M8_IO_DO_TOP_BRUSH_DOWN,
        .running = &s_lift_running,
    };
    s_rear_lock_relay_ctx = (relay_ctx_t){
        .pin_fwd = M8_IO_DO_ROD_EXTEND,
        .pin_bwd = M8_IO_DO_ROD_RETRACT,
        .running = &s_rear_lock_running,
    };

    memset(&cfg, 0, sizeof(cfg));
    cfg.motor_count  = M8_MOTOR_COUNT;
    cfg.driver_count = M8_MOTOR_COUNT;
    cfg.tick_ms      = MOTOR_TICK_INTERVAL_MS; /* 所有电机共用 20ms 节拍 */
    cfg.watchdog_ms  = CFG_GANTRY_WATCHDOG_MS;

    /* 侧刷/顶刷共用一台 VFD，双向互斥：任一方运行相关态时另一方不可启动 */
    cfg.interlocks[0].kind = MOTOR_INTERLOCK_MUTEX;
    cfg.interlocks[0].a    = M8_MOTOR_BRUSH_SIDE;
    cfg.interlocks[0].b    = M8_MOTOR_BRUSH_TOP;
    cfg.interlocks[1].kind = MOTOR_INTERLOCK_MUTEX;
    cfg.interlocks[1].a    = M8_MOTOR_BRUSH_TOP;
    cfg.interlocks[1].b    = M8_MOTOR_BRUSH_SIDE;
    cfg.interlock_count    = 2;

    /* --- 电机 0：龙门行走 VFD --- */
    m                       = &cfg.motors[M8_MOTOR_GANTRY];
    m->driver_index         = M8_MOTOR_GANTRY;
    m->has_encoder          = true;
    m->cap_position_move    = true;
    m->cooldown_ms          = CFG_GANTRY_COOLDOWN_MS;
    m->reversal_stop_ms     = CFG_GANTRY_REVERSAL_STOP_MS;
    m->accel_ms             = CFG_GANTRY_ACCEL_MS;
    m->decel_ms             = CFG_GANTRY_DECEL_MS;
    m->default_max_move_ms  = CFG_GANTRY_DEFAULT_MAX_MOVE_MS;
    m->enc_stall_ticks      = CFG_GANTRY_ENC_STALL_TICKS;
    m->enc_jump_max         = CFG_GANTRY_ENC_JUMP_MAX;
    m->enc_escalate         = false;
    m->slow_freq            = CFG_GANTRY_GEAR_FREQ_LOW;
    m->gear_freq[0]         = CFG_GANTRY_GEAR_FREQ_LOW;
    m->gear_freq[1]         = CFG_GANTRY_GEAR_FREQ_HIGH;
    m->gear_count           = CFG_GANTRY_GEAR_COUNT;
    m->mon.monitor_current  = false;
    m->mon.monitor_feedback = false;

    /* --- 电机 1/2：侧刷/顶刷 VFD（共用一台物理驱动器，靠接触器切换，
     *     prep_required 触发 prepare() 完成切换后才允许进入 RUNNING）--- */
    m                       = &cfg.motors[M8_MOTOR_BRUSH_SIDE];
    m->driver_index         = M8_MOTOR_BRUSH_SIDE;
    m->has_encoder          = false;
    m->cap_position_move    = false;
    m->prep_required        = true;
    m->cooldown_ms          = CFG_BRUSH_COOLDOWN_MS;
    m->reversal_stop_ms     = 0;
    m->accel_ms             = CFG_BRUSH_ACCEL_MS;
    m->decel_ms             = CFG_BRUSH_DECEL_MS;
    m->default_max_move_ms  = CFG_BRUSH_DEFAULT_MAX_MOVE_MS;
    m->slow_freq            = CFG_BRUSH_GEAR_FREQ_LOW;
    m->gear_freq[0]         = CFG_BRUSH_GEAR_FREQ_LOW;
    m->gear_freq[1]         = CFG_BRUSH_GEAR_FREQ_NORMAL;
    m->gear_freq[2]         = CFG_BRUSH_GEAR_FREQ_HIGH;
    m->gear_count           = CFG_BRUSH_GEAR_COUNT;
    m->mon.monitor_current  = true;
    m->mon.cur_max_steady   = CFG_BRUSH_CUR_MAX_STEADY;
    m->mon.cur_min_steady   = CFG_BRUSH_CUR_MIN_STEADY;
    m->mon.cur_max_accel    = CFG_BRUSH_CUR_MAX_STEADY;
    m->mon.cur_min_accel    = 0;
    m->mon.cur_confirm_ms   = CFG_BRUSH_CUR_CONFIRM_MS;
    m->mon.startup_delay_ms = CFG_BRUSH_CUR_STARTUP_DELAY_MS;
    m->mon.monitor_feedback = false;

    m               = &cfg.motors[M8_MOTOR_BRUSH_TOP];
    *m              = cfg.motors[M8_MOTOR_BRUSH_SIDE];
    m->driver_index = M8_MOTOR_BRUSH_TOP;

    /* --- 电机 3：升降继电器 --- */
    m                      = &cfg.motors[M8_MOTOR_LIFT];
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
    m                      = &cfg.motors[M8_MOTOR_REAR_LOCK];
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
    m                       = &cfg.motors[M8_MOTOR_FAN];
    m->driver_index         = M8_MOTOR_FAN;
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

    LOG_INFO("m8_motor_exec_init ok (%d motors)", M8_MOTOR_COUNT);
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
