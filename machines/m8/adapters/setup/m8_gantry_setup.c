/**
 * @file    m8_gantry_setup.c
 * @brief   M8 机型龙门行走适配层：VFD 驱动端口实现 + 编码器 + 限位回调 + MCC 配置注入。
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "machines/m8/adapters/setup/m8_gantry_setup.h"
#include "machines/m8/adapters/setup/m8_motor_tick.h"
#include "domain/device/mechanism/gantry.h"
#include "machines/m8/config/m8_gantry_config.h"
#include "machines/m8/config/m8_io_pins.h"
#include "machines/m8/config/m8_vfd_table.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/hal/hal_io_port.h"
#include "common/vfd_types.h"
#include "common/log.h"
#include <stddef.h>
#include <string.h>

/* -------------------- 静态 DO 写辅助 -------------------- */

static sw_err_t io_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

/* -------------------- MCC 物理驱动器端口实现 -------------------- */
/*
 * 龙门 VFD（HAL_VFD_GANTRY）已由 m8_vfd_setup() 完成 Modbus 连接与 DO 绑定。
 * 此处通过 hal_vfd_get_ops() 转发 MCC 驱动器回调，并额外管理 GANTRY_HIGH_SPEED DO。
 */

static void gantry_vfd_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t freq_hz;
    hal_vfd_gear_t gear;

    (void)ctx;

    if (vfd == NULL) {
        return;
    }

    if (freq_centi_hz > 0) {
        /* 厘赫转 Hz（向上取整，确保最低 1Hz） */
        freq_hz = (uint16_t)((freq_centi_hz + 99) / 100);
        /* 高速 DO：freq_hz 达到阈值时吸合，选择 VFD 内部高速预设 */
        (void)io_do_set(M8_IO_DO_GANTRY_HIGH_SPEED,
                        freq_hz >= CFG_GANTRY_HIGH_SPEED_THRESHOLD_HZ);
        (void)vfd->set_freq(HAL_VFD_GANTRY, freq_hz);
        gear = (dir == MOTOR_DIR_FORWARD) ? (hal_vfd_gear_t)1 : (hal_vfd_gear_t)(-1);
        (void)vfd->run(HAL_VFD_GANTRY, gear);
    } else {
        /* 停止：先断开高速 DO，再停 VFD */
        (void)io_do_set(M8_IO_DO_GANTRY_HIGH_SPEED, false);
        (void)vfd->stop(HAL_VFD_GANTRY);
    }
}

static void gantry_vfd_cutoff(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    (void)io_do_set(M8_IO_DO_GANTRY_HIGH_SPEED, false);
    if (vfd != NULL) {
        (void)vfd->stop(HAL_VFD_GANTRY);
    }
}

static bool gantry_vfd_reset(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    (void)vfd->fault_reset(HAL_VFD_GANTRY);
    return true;
}

static bool gantry_vfd_is_running(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    hal_vfd_state_t st = vfd->get_state(HAL_VFD_GANTRY);
    return (st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV);
}

static int gantry_vfd_current(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t val = 0;

    (void)ctx;
    if (vfd == NULL) {
        return 0;
    }
    /* 读缓存电流（单位 0.01A） */
    (void)vfd->get_cached(HAL_VFD_GANTRY, HAL_VFD_REG_CURRENT, &val);
    return (int)val;
}

/* -------------------- 编码器端口回调 -------------------- */

static int64_t gantry_enc_raw(void *ctx)
{
    const hal_io_ops_t *io = hal_io_get_ops();
    int val;

    (void)ctx;
    if ((io == NULL) || (io->pulse_read == NULL)) {
        return 0;
    }
    val = io->pulse_read(M8_IO_DI_GANTRY_ENCODER_PULSE);
    /* pulse_read 返回负值表示读取失败 */
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

/* -------------------- 限位传感器端口回调 -------------------- */

static bool gantry_limit(void *ctx, int motor, motor_limit_kind_t kind)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    (void)ctx;
    (void)motor;
    if ((io == NULL) || (io->di_read == NULL)) {
        return false;
    }
    switch (kind) {
    case MOTOR_LIMIT_POS:
        return io->di_read(M8_IO_DI_GANTRY_FWD_LIMIT);
    case MOTOR_LIMIT_NEG:
    case MOTOR_LIMIT_ORIGIN:
        /* 后限位兼作原点：motor_home() 触发后自动建立编码器可信基准 */
        return io->di_read(M8_IO_DI_GANTRY_REV_LIMIT);
    default:
        return false;
    }
}

/* -------------------- 急停端口回调 -------------------- */

static bool gantry_estop(void *ctx)
{
    /* 急停由上层安全模块统一处理，此处不重复接入 */
    (void)ctx;
    return false;
}

/* -------------------- 时间源 -------------------- */

#include <time.h>

static uint64_t gantry_now_ms(void *ctx)
{
    struct timespec ts;
    (void)ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

/* -------------------- 静态 MCC 对象 -------------------- */

static motor_executor_t s_gantry_exec;

static motor_encoder_t s_gantry_encoder = {
    .raw = gantry_enc_raw,
    .zero = gantry_enc_zero,
    .ctx = NULL,
};

static motor_encoder_t * const s_gantry_encoders[1] = { &s_gantry_encoder };

static motor_driver_t s_gantry_driver = {
    .set_output  = gantry_vfd_set_output,
    .cutoff      = gantry_vfd_cutoff,
    .reset       = gantry_vfd_reset,
    .prepare     = NULL,
    .is_running  = gantry_vfd_is_running,
    .current     = gantry_vfd_current,
    .temperature = NULL,
    .voltage     = NULL,
    .status      = NULL,
    .ctx         = NULL,
};

static motor_driver_t * const s_gantry_driver_ptr = &s_gantry_driver;

static motor_clock_t s_gantry_clock = {
    .now_ms = gantry_now_ms,
    .ctx    = NULL,
};

static motor_sensors_t s_gantry_sensors = {
    .limit = gantry_limit,
    .ctx   = NULL,
};

static motor_estop_t s_gantry_estop = {
    .active = gantry_estop,
    .ctx    = NULL,
};

/* -------------------- 公共函数 -------------------- */

sw_err_t m8_gantry_setup(void)
{
    sw_err_t ret;
    motor_config_t cfg;
    motor_motor_cfg_t *m;
    motor_ports_t ports;
    motor_init_result_t init_res;

    /* 检查前置依赖 */
    if (hal_vfd_get_ops() == NULL) {
        LOG_ERROR("m8_gantry_setup: hal_vfd not registered");
        return SW_ERR_NOT_INIT;
    }
    if (hal_io_get_ops() == NULL) {
        LOG_ERROR("m8_gantry_setup: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    /* 构造 MCC 配置：1 个电机，1 个 VFD 驱动器，带编码器 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.motor_count     = 1;
    cfg.driver_count    = 1;
    cfg.interlock_count = 0;
    cfg.tick_ms         = CFG_GANTRY_TICK_MS;
    cfg.watchdog_ms     = CFG_GANTRY_WATCHDOG_MS;

    /* 电机 0：龙门行走 VFD */
    m = &cfg.motors[0];
    m->driver_index        = 0;
    m->has_encoder         = true;
    m->cap_position_move   = true;
    m->cooldown_ms         = CFG_GANTRY_COOLDOWN_MS;
    m->reversal_stop_ms    = CFG_GANTRY_REVERSAL_STOP_MS;
    m->accel_ms            = CFG_GANTRY_ACCEL_MS;
    m->decel_ms            = CFG_GANTRY_DECEL_MS;
    m->prep_required       = false;
    m->has_soft_limit      = false;
    m->default_max_move_ms = CFG_GANTRY_DEFAULT_MAX_MOVE_MS;
    m->enc_stall_ticks     = CFG_GANTRY_ENC_STALL_TICKS;
    m->enc_jump_max        = CFG_GANTRY_ENC_JUMP_MAX;
    m->enc_escalate        = false;  /* 编码器告警不升级为故障 */
    m->slow_freq           = CFG_GANTRY_GEAR_FREQ_LOW;  /* 回原点以低速挡行进 */

    /* 挡位频率映射 */
    m->gear_freq[0] = CFG_GANTRY_GEAR_FREQ_LOW;
    m->gear_freq[1] = CFG_GANTRY_GEAR_FREQ_HIGH;
    m->gear_count   = CFG_GANTRY_GEAR_COUNT;

    /* 不启用电流监测（龙门 VFD 通过 GANTRY_ALARM DI 上报故障，不依赖电流判定） */
    m->mon.monitor_current  = false;
    m->mon.monitor_feedback = false;
    m->mon.monitor_temp     = false;
    m->mon.monitor_voltage  = false;

    /* 端口集合 */
    memset(&ports, 0, sizeof(ports));
    ports.clock    = &s_gantry_clock;
    ports.drivers  = &s_gantry_driver_ptr;
    ports.encoders = s_gantry_encoders;
    ports.sensors  = &s_gantry_sensors;
    ports.estop    = &s_gantry_estop;

    /* 初始化 MCC 执行器 */
    init_res = motor_init(&s_gantry_exec, &cfg, &ports);
    if (!init_res.ok) {
        LOG_ERROR("m8_gantry_setup: motor_init failed: %s", init_res.error);
        return SW_ERR_HW;
    }

    /* 注入到 domain gantry 层 */
    ret = gantry_init(&s_gantry_exec);
    if (ret != SW_OK) {
        LOG_ERROR("m8_gantry_setup: gantry_init failed ret=%d", (int)ret);
        return ret;
    }

    /* 注册到统一 tick 管理器 */
    ret = m8_motor_tick_register(gantry_tick);
    if (ret != SW_OK) {
        LOG_ERROR("m8_gantry_setup: m8_motor_tick_register 失败 ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_gantry_setup ok");
    return SW_OK;
}
