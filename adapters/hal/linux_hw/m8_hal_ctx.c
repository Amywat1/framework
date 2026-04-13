/**
 * @file    m8_hal_ctx.c
 * @brief   M8 linux_hw 适配器内部共享状态实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "driver/drv_vfd.h"
#include "driver/drv_stepper.h"
#include "driver/drv_io.h"
#include "common/log.h"

#include <pthread.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * VFD 实例（静态分配，由 m8_linux_hw_init 初始化）
 * ------------------------------------------------------------------------- */
static drv_vfd_t s_vfd_brush;
static drv_vfd_t s_vfd_gantry;

/* -------------------------------------------------------------------------
 * 龙门运动方向（motion 更新，encoder callback 读取）
 * 用 volatile，不需要精确原子性（最坏情况：一次 tick 方向暂时错误，可接受）
 * ------------------------------------------------------------------------- */
static volatile int s_gantry_is_fwd = 1; /* 1=前进，0=后退 */

/* -------------------------------------------------------------------------
 * 龙门位置计数器（encoder callback 和 get_pos 可能在不同线程，用 mutex 保护）
 * ------------------------------------------------------------------------- */
static int32_t         s_gantry_pos = 0;
static pthread_mutex_t s_pos_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * VFD 事件路由（驱动层事件 → event_bus）
 * ------------------------------------------------------------------------- */
#include "core/event_bus/event_bus.h"
#include "domain/model/alarm_code.h"

/* VFD 事件回调：由驱动层触发（当前驱动层未主动调用此回调，
 * 正常故障检测由 m8_alarm_adapt 的周期性读取完成，
 * 此回调作为备用兜底保留。） */
static void brush_vfd_event_cb(int event_code)
{
    (void)event_code;
    (void)event_publish(EVT_HW_VFD_BRUSH_FAULT, ALARM_CODE_VFD_BRUSH);
}

static void gantry_vfd_event_cb(int event_code)
{
    (void)event_code;
    (void)event_publish(EVT_HW_VFD_GANTRY_FAULT, ALARM_CODE_VFD_GANTRY);
}

/* -------------------------------------------------------------------------
 * m8_linux_hw_init
 * ------------------------------------------------------------------------- */
sw_err_t m8_linux_hw_init(void)
{
    sw_err_t ret;

    /* 刷子 VFD：仅正转（无反转引脚），接触器上电时全部断开 */
    memset(&s_vfd_brush, 0, sizeof(s_vfd_brush));
    ret = drv_vfd_init(&s_vfd_brush,
                       M8_VFD_SERIAL_PORT, M8_VFD_BAUD, M8_VFD_BRUSH_ADDR,
                       M8_DO_BRUSH_FWD,
                       false, M8_DO_BRUSH_FWD,  /* has_rev=false，rev引脚占位 */
                       M8_DO_BRUSH_RST);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_linux_hw_init: vfd_brush init failed");
        return ret;
    }
    drv_vfd_register_event_cb(&s_vfd_brush, brush_vfd_event_cb);
    (void)drv_io_do_set(M8_DO_TOP_BRUSH_ACT,  false);
    (void)drv_io_do_set(M8_DO_SIDE_BRUSH_ACT, false);

    /* 龙门 VFD：支持正反转 */
    memset(&s_vfd_gantry, 0, sizeof(s_vfd_gantry));
    ret = drv_vfd_init(&s_vfd_gantry,
                       M8_VFD_SERIAL_PORT, M8_VFD_BAUD, M8_VFD_GANTRY_ADDR,
                       M8_DO_GANTRY_FWD,
                       true, M8_DO_GANTRY_REV,
                       M8_DO_GANTRY_RST);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_linux_hw_init: vfd_gantry init failed");
        return ret;
    }
    drv_vfd_register_event_cb(&s_vfd_gantry, gantry_vfd_event_cb);

    /* 步进电机初始化 */
    ret = drv_stepper_init();
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_linux_hw_init: drv_stepper_init failed");
        return ret;
    }

    LOG_INFO("m8_linux_hw_init ok");
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * VFD 实例访问器
 * ------------------------------------------------------------------------- */
drv_vfd_t *m8_ctx_vfd_brush(void)   { return &s_vfd_brush; }
drv_vfd_t *m8_ctx_vfd_gantry(void)  { return &s_vfd_gantry; }

/* -------------------------------------------------------------------------
 * 龙门方向
 * ------------------------------------------------------------------------- */
void m8_ctx_set_gantry_fwd(bool is_fwd)  { s_gantry_is_fwd = is_fwd ? 1 : 0; }
bool m8_ctx_gantry_is_fwd(void)          { return (s_gantry_is_fwd != 0); }

/* -------------------------------------------------------------------------
 * 龙门位置计数器
 * ------------------------------------------------------------------------- */
int32_t m8_ctx_get_gantry_pos(void)
{
    int32_t pos;
    pthread_mutex_lock(&s_pos_mutex);
    pos = s_gantry_pos;
    pthread_mutex_unlock(&s_pos_mutex);
    return pos;
}

void m8_ctx_reset_gantry_pos(void)
{
    pthread_mutex_lock(&s_pos_mutex);
    s_gantry_pos = 0;
    pthread_mutex_unlock(&s_pos_mutex);
}

void m8_ctx_encoder_tick(void)
{
    pthread_mutex_lock(&s_pos_mutex);
    if (s_gantry_is_fwd)
    {
        s_gantry_pos++;
    }
    else
    {
        s_gantry_pos--;
    }
    pthread_mutex_unlock(&s_pos_mutex);
}
