/**
 * @file    m8_brush_setup.c
 * @brief   M8 机型刷子执行器绑定（VFD + 接触器，注入 brush domain）
 * @author  HUWANGWEI
 * @date    2026-06-30
 *
 * @note    brush_id → motor_id 映射在 s_brush_map[] 中集中管理；
 *          新增刷子时只需追加一条记录并在 motor pre_start 中扩展接触器逻辑。
 *          接触器切换由 motor PENDING 机制异步完成，不阻塞调用线程。
 */

#include "machines/m8/adapters/setup/m8_brush_setup.h"
#include "domain/device/mechanism/brush.h"
#include "domain/device/actuator/motor/motor.h"
#include "machines/m8/config/m8_motor_table.h"
#include "ports/hal/hal_io_port.h"
#include "machines/m8/config/m8_io_pins.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * brush_id → motor_id 映射表
 * 新增独立电机的刷子：追加一条记录，motor_id 填对应的新 motor ID。
 * 共用同一 VFD 的刷子：motor_id 填同一值，motor 层通过 pre_start 切换接触器。
 * ------------------------------------------------------------------------- */
static const struct
{
    brush_id_t id;
    int        motor_id;
} s_brush_map[] = {
    { BRUSH_ID_TOP,  MOTOR_BRUSH },
    { BRUSH_ID_SIDE, MOTOR_BRUSH },
};

static int find_motor_id(brush_id_t id)
{
    for (int i = 0; i < (int)(sizeof(s_brush_map) / sizeof(s_brush_map[0])); i++)
    {
        if (s_brush_map[i].id == id)
        {
            return s_brush_map[i].motor_id;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * 接触器物理状态（与 brush.c 逻辑状态解耦）
 * ------------------------------------------------------------------------- */
static brush_id_t s_hw_active = BRUSH_ID_NONE;

static sw_err_t m8_brush_do_set(brush_id_t id, bool on)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->do_set == NULL)) { return SW_ERR_NOT_INIT; }

    switch (id)
    {
        case BRUSH_ID_SIDE: return io->do_set(M8_IO_DO_SIDE_BRUSH_ACT, on);
        case BRUSH_ID_TOP:  return io->do_set(M8_IO_DO_TOP_BRUSH_ACT,  on);
        default:            return SW_ERR_PARAM;
    }
}

/* -------------------------------------------------------------------------
 * motor pre_start 回调：VFD 停止延迟满足后完成接触器切换
 * ------------------------------------------------------------------------- */
static sw_err_t m8_brush_pre_start(int motor_id, int speed_ref, void *ctx)
{
    brush_id_t new_id = brush_get_active();
    sw_err_t   ret    = SW_OK;

    (void)motor_id;
    (void)speed_ref;
    (void)ctx;

    if (new_id == BRUSH_ID_NONE) { return SW_ERR_PARAM; }

    if ((s_hw_active != BRUSH_ID_NONE) && (s_hw_active != new_id))
    {
        ret = m8_brush_do_set(s_hw_active, false);
        if (ret != SW_OK)
        {
            LOG_WARN("m8_brush_pre_start: release old contactor failed ret=%d", (int)ret);
        }
        s_hw_active = BRUSH_ID_NONE;
    }

    if (s_hw_active != new_id)
    {
        ret = m8_brush_do_set(new_id, true);
        if (ret != SW_OK)
        {
            LOG_ERROR("m8_brush_pre_start: set contactor id=%d failed ret=%d",
                      (int)new_id, (int)ret);
            return ret;
        }
        s_hw_active = new_id;
        LOG_INFO("m8_brush_pre_start: contactor -> %s",
                 (new_id == BRUSH_ID_TOP) ? "top" : "side");
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * brush_actuator_ops_t 实现
 * ------------------------------------------------------------------------- */
static sw_err_t m8_brush_start_freq(brush_id_t id, uint16_t speed_ref)
{
    int motor_id = find_motor_id(id);

    if (motor_id < 0) { return SW_ERR_PARAM; }
    return motor_hold(motor_id, (int)speed_ref);
}

static sw_err_t m8_brush_start_gear(brush_id_t id, uint8_t gear)
{
    int motor_id = find_motor_id(id);

    if (motor_id < 0) { return SW_ERR_PARAM; }
    return motor_hold_gear(motor_id, (motor_gear_t)gear);
}

static sw_err_t m8_brush_stop(brush_id_t id)
{
    int motor_id = find_motor_id(id);

    if (motor_id < 0) { return SW_ERR_PARAM; }
    return motor_stop(motor_id);
}

static bool m8_brush_is_fault(brush_id_t id)
{
    int motor_id = find_motor_id(id);

    if (motor_id < 0) { return false; }
    return motor_get_state(motor_id) == MOTOR_STATE_FAULT;
}

static uint16_t m8_brush_get_current(brush_id_t id)
{
    int motor_id = find_motor_id(id);

    if (motor_id < 0) { return 0U; }
    return motor_get_current(motor_id);
}

static const brush_actuator_ops_t s_brush_ops = {
    .start_freq  = m8_brush_start_freq,
    .start_gear  = m8_brush_start_gear,
    .stop        = m8_brush_stop,
    .off         = NULL,          /* 释放接触器由下次 pre_start 处理，无需专用 off */
    .is_fault    = m8_brush_is_fault,
    .get_current = m8_brush_get_current,
};

/* -------------------------------------------------------------------------
 * 入口
 * ------------------------------------------------------------------------- */
sw_err_t m8_brush_setup(void)
{
    sw_err_t ret;

    if (hal_io_get_ops() == NULL)
    {
        LOG_ERROR("m8_brush_setup: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = brush_init(&s_brush_ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_brush_setup: brush_init failed ret=%d", (int)ret);
        return ret;
    }

    s_hw_active = BRUSH_ID_NONE;
    ret = motor_set_pre_start_cb(MOTOR_BRUSH, m8_brush_pre_start, NULL);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_brush_setup: motor_set_pre_start_cb failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_brush_setup ok");
    return SW_OK;
}
