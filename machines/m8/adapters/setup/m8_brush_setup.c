/**
 * @file    m8_brush_setup.c
 * @brief   M8 机型刷子控制适配层：VFD 驱动端口实现 + 接触器 DO 控制 + MCC 配置注入。
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "machines/m8/adapters/setup/m8_brush_setup.h"
#include "machines/m8/adapters/setup/m8_motor_tick.h"
#include "domain/device/mechanism/brush.h"
#include "machines/m8/config/m8_brush_config.h"
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
 * 刷子 VFD（HAL_VFD_BRUSH）已由 m8_vfd_setup() 完成 Modbus 连接与 DO 绑定，
 * 此处仅通过 hal_vfd_get_ops() 获取操作接口并转发 MCC 的驱动器回调。
 */

static void brush_vfd_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    (void)dir;  /* 刷子 VFD 无反转引脚，始终正转 */

    if (vfd == NULL) {
        return;
    }

    if (freq_centi_hz > 0) {
        /* 厘赫转 Hz（向上取整，确保最低 1Hz） */
        uint16_t freq_hz = (uint16_t)((freq_centi_hz + 99) / 100);
        (void)vfd->set_freq(HAL_VFD_BRUSH, freq_hz);
        (void)vfd->run(HAL_VFD_BRUSH, 1);  /* 正转挡位 */
    } else {
        (void)vfd->stop(HAL_VFD_BRUSH);
    }
}

static void brush_vfd_cutoff(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd != NULL) {
        (void)vfd->stop(HAL_VFD_BRUSH);
    }
}

static bool brush_vfd_reset(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    (void)vfd->fault_reset(HAL_VFD_BRUSH);
    return true;
}

static bool brush_vfd_is_running(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    (void)ctx;
    if (vfd == NULL) {
        return false;
    }
    hal_vfd_state_t st = vfd->get_state(HAL_VFD_BRUSH);
    return (st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV);
}

static int brush_vfd_current(void *ctx)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t val = 0;

    (void)ctx;
    if (vfd == NULL) {
        return 0;
    }
    /* 读缓存电流（单位 0.01A，与 MCC 电流监测阈值同量纲） */
    (void)vfd->get_cached(HAL_VFD_BRUSH, HAL_VFD_REG_CURRENT, &val);
    return (int)val;
}

/* -------------------- 接触器操作回调 -------------------- */

static sw_err_t m8_contactor_on(void *ctx, brush_id_t id)
{
    (void)ctx;
    switch (id) {
    case BRUSH_SIDE:
        return io_do_set(M8_IO_DO_SIDE_BRUSH_ACT, true);
    case BRUSH_TOP:
        return io_do_set(M8_IO_DO_TOP_BRUSH_ACT, true);
    default:
        return SW_ERR_PARAM;
    }
}

static sw_err_t m8_contactor_off(void *ctx, brush_id_t id)
{
    (void)ctx;
    switch (id) {
    case BRUSH_SIDE:
        return io_do_set(M8_IO_DO_SIDE_BRUSH_ACT, false);
    case BRUSH_TOP:
        return io_do_set(M8_IO_DO_TOP_BRUSH_ACT, false);
    default:
        return SW_ERR_PARAM;
    }
}

/* -------------------- 限位与急停（刷子无限位，急停来自 m8_sensor） -------------------- */

static bool brush_limit(void *ctx, int motor, motor_limit_kind_t kind)
{
    /* 刷子电机无硬限位，始终返回未触发 */
    (void)ctx;
    (void)motor;
    (void)kind;
    return false;
}

static bool brush_estop(void *ctx)
{
    /* 急停由上层安全模块统一处理，此处不重复接入，返回未触发 */
    (void)ctx;
    return false;
}

/* -------------------- 时间源 -------------------- */

#include <time.h>

static uint64_t brush_now_ms(void *ctx)
{
    struct timespec ts;
    (void)ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

/* -------------------- 静态 MCC 对象 -------------------- */

static motor_executor_t s_brush_exec;

/* 无编码器：提供长度为 1 的 NULL 指针数组，满足 MCC "长度=电机数" 要求 */
static motor_encoder_t * const s_brush_encoders[1] = { NULL };

static motor_driver_t s_brush_driver = {
    .set_output  = brush_vfd_set_output,
    .cutoff      = brush_vfd_cutoff,
    .reset       = brush_vfd_reset,
    .prepare     = NULL,        /* 无需预备动作 */
    .is_running  = brush_vfd_is_running,
    .current     = brush_vfd_current,
    .temperature = NULL,
    .voltage     = NULL,
    .status      = NULL,
    .ctx         = NULL,
};

static motor_driver_t * const s_brush_driver_ptr = &s_brush_driver;

static motor_clock_t s_brush_clock = {
    .now_ms = brush_now_ms,
    .ctx    = NULL,
};

static motor_sensors_t s_brush_sensors = {
    .limit = brush_limit,
    .ctx   = NULL,
};

static motor_estop_t s_brush_estop = {
    .active = brush_estop,
    .ctx    = NULL,
};

/* -------------------- 公共函数 -------------------- */

sw_err_t m8_brush_setup(void)
{
    sw_err_t ret;

    /* 检查前置依赖 */
    if (hal_vfd_get_ops() == NULL) {
        LOG_ERROR("m8_brush_setup: hal_vfd not registered");
        return SW_ERR_NOT_INIT;
    }
    if (hal_io_get_ops() == NULL) {
        LOG_ERROR("m8_brush_setup: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    /* 构造 MCC 配置：1 个电机共享 1 个 VFD 驱动器 */
    motor_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.motor_count    = 1;
    cfg.driver_count   = 1;
    cfg.interlock_count = 0;
    cfg.tick_ms        = CFG_BRUSH_TICK_MS;
    cfg.watchdog_ms    = CFG_BRUSH_WATCHDOG_MS;

    /* 电机 0：刷子 VFD（侧刷/顶刷共用，接触器由 domain brush 层切换） */
    motor_motor_cfg_t *m = &cfg.motors[0];
    m->driver_index        = 0;
    m->has_encoder         = false;
    m->cap_position_move   = false;
    m->cooldown_ms         = CFG_BRUSH_COOLDOWN_MS;
    m->reversal_stop_ms    = 0;    /* 不换向 */
    m->accel_ms            = CFG_BRUSH_ACCEL_MS;
    m->decel_ms            = CFG_BRUSH_DECEL_MS;
    m->prep_required       = false;
    m->has_soft_limit      = false;
    m->default_max_move_ms = CFG_BRUSH_DEFAULT_MAX_MOVE_MS;
    m->enc_stall_ticks     = 0;
    m->enc_jump_max        = 0;

    /* 挡位频率映射 */
    m->gear_freq[0] = CFG_BRUSH_GEAR_FREQ_LOW;
    m->gear_freq[1] = CFG_BRUSH_GEAR_FREQ_NORMAL;
    m->gear_freq[2] = CFG_BRUSH_GEAR_FREQ_HIGH;
    m->gear_count   = CFG_BRUSH_GEAR_COUNT;

    /* 电流监测 */
    m->mon.monitor_current    = true;
    m->mon.cur_max_steady     = CFG_BRUSH_CUR_MAX_STEADY;
    m->mon.cur_min_steady     = CFG_BRUSH_CUR_MIN_STEADY;
    m->mon.cur_max_accel      = CFG_BRUSH_CUR_MAX_STEADY;  /* 加速段与匀速段同阈值 */
    m->mon.cur_min_accel      = 0;                          /* 加速段不检空转 */
    m->mon.cur_confirm_ms     = CFG_BRUSH_CUR_CONFIRM_MS;
    m->mon.startup_delay_ms   = CFG_BRUSH_CUR_STARTUP_DELAY_MS;
    m->mon.monitor_feedback   = false;  /* VFD 无独立运行反馈 DI */
    m->mon.monitor_temp       = false;
    m->mon.monitor_voltage    = false;

    /* 端口集合 */
    motor_ports_t ports;
    memset(&ports, 0, sizeof(ports));
    ports.clock    = &s_brush_clock;
    ports.drivers  = &s_brush_driver_ptr;
    ports.encoders = s_brush_encoders;
    ports.sensors  = &s_brush_sensors;
    ports.estop    = &s_brush_estop;

    /* 初始化 MCC 执行器 */
    motor_init_result_t init_res = motor_init(&s_brush_exec, &cfg, &ports);
    if (!init_res.ok) {
        LOG_ERROR("m8_brush_setup: motor_init failed: %s", init_res.error);
        return SW_ERR_HW;
    }

    /* 注入到 domain brush 层 */
    ret = brush_init(
        &s_brush_exec,
        &(brush_contactor_ops_t){
            .set_on  = m8_contactor_on,
            .set_off = m8_contactor_off,
            .ctx     = NULL,
        },
        &(brush_contactor_cfg_t){
            .release_ms = CFG_BRUSH_CONTACTOR_RELEASE_MS,
            .close_ms   = CFG_BRUSH_CONTACTOR_CLOSE_MS,
        });
    if (ret != SW_OK) {
        LOG_ERROR("m8_brush_setup: brush_init failed ret=%d", (int)ret);
        return ret;
    }

    ret = m8_motor_tick_register(brush_tick);
    if (ret != SW_OK) {
        LOG_ERROR("m8_brush_setup: m8_motor_tick_register 失败 ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_brush_setup ok");
    return SW_OK;
}
