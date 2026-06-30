/**
 * @file    m8_brush_setup.c
 * @brief   M8 机型刷子接触器 DO 绑定（注册为 motor pre_start 回调）
 * @author  HUWANGWEI
 * @date    2026-06-30
 *
 * @note    接触器切换时序：VFD 停止 → post_stop_delay（由 motor 层计时）→
 *          m8_brush_pre_start 回调（释放旧接触器、吸合新接触器）→ VFD 重新输出。
 *          整个过程在 motor_tick 线程中异步完成，不阻塞洗车执行线程。
 */

#include "machines/m8/adapters/setup/m8_brush_setup.h"
#include "domain/device/unit/brush.h"
#include "domain/device/actuator/motor/motor.h"
#include "machines/m8/config/m8_motor_table.h"
#include "ports/hal/hal_io_port.h"
#include "machines/m8/config/m8_io_pins.h"
#include "common/log.h"

/* 跟踪接触器物理状态（与 brush.c 的逻辑状态解耦） */
typedef struct
{
    brush_id_t hw_active; /**< 当前已吸合的接触器对应的刷子 ID */
} m8_brush_ctx_t;

static m8_brush_ctx_t s_ctx = { .hw_active = BRUSH_ID_NONE };

static sw_err_t m8_brush_do_set(brush_id_t id, bool on)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    switch (id)
    {
        case BRUSH_ID_SIDE:
            return io->do_set(M8_IO_DO_SIDE_BRUSH_ACT, on);
        case BRUSH_ID_TOP:
            return io->do_set(M8_IO_DO_TOP_BRUSH_ACT, on);
        default:
            return SW_ERR_PARAM;
    }
}

/**
 * @brief  motor pre_start 回调：在 VFD 停止延迟满足后，完成接触器切换
 * @note   由 motor_tick 线程在锁外调用，禁止阻塞。
 */
static sw_err_t m8_brush_pre_start(int motor_id, int speed_ref, void *ctx)
{
    m8_brush_ctx_t *bc      = (m8_brush_ctx_t *)ctx;
    brush_id_t      new_id  = brush_get_active();
    sw_err_t        ret     = SW_OK;

    (void)motor_id;
    (void)speed_ref;

    if (new_id == BRUSH_ID_NONE)
    {
        return SW_ERR_PARAM;
    }

    /* 释放旧接触器（若与目标不同） */
    if ((bc->hw_active != BRUSH_ID_NONE) && (bc->hw_active != new_id))
    {
        ret = m8_brush_do_set(bc->hw_active, false);
        if (ret != SW_OK)
        {
            LOG_WARN("m8_brush_pre_start: release old contactor failed ret=%d", (int)ret);
        }
        bc->hw_active = BRUSH_ID_NONE;
    }

    /* 吸合新接触器 */
    if (bc->hw_active != new_id)
    {
        ret = m8_brush_do_set(new_id, true);
        if (ret != SW_OK)
        {
            LOG_ERROR("m8_brush_pre_start: set contactor id=%d failed ret=%d",
                      (int)new_id, (int)ret);
            return ret;
        }
        bc->hw_active = new_id;
        LOG_INFO("m8_brush_pre_start: contactor -> %s",
                 (new_id == BRUSH_ID_TOP) ? "top" : "side");
    }

    return SW_OK;
}

sw_err_t m8_brush_setup(void)
{
    sw_err_t ret;

    if (hal_io_get_ops() == NULL)
    {
        LOG_ERROR("m8_brush_setup: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = brush_init();
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_brush_setup: brush_init failed ret=%d", (int)ret);
        return ret;
    }

    s_ctx.hw_active = BRUSH_ID_NONE;
    ret = motor_set_pre_start_cb(MOTOR_BRUSH, m8_brush_pre_start, &s_ctx);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_brush_setup: motor_set_pre_start_cb failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_brush_setup ok");
    return SW_OK;
}
