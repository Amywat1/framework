/**
 * @file    emergency_handler.c
 * @brief   急停与安全 LOCKOUT 统一动作处理
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    事件处理逻辑：
 *
 *   EVT_HW_ESTOP_ON（高优先级）
 *     → 硬件已断电，执行器物理停止；置位 s_estop_active，禁止立即执行安全归位
 *       （急停断电中刷子无法移动）
 *
 *   EVT_HW_ESTOP_OFF
 *     → 电源恢复；清除 s_estop_active，执行安全归位（刷子回零），
 *       发布 EVT_SAFETY_HOME_DONE 通知 device_fsm 转 FAULT
 *
 *   EVT_SAFETY_LOCKOUT（报警聚合为 LOCKOUT 级别，非急停路径）
 *     → 中止洗车流程
 *     → 若非急停状态：立即执行安全归位并发布 EVT_SAFETY_HOME_DONE
 *     → 若在急停中：安全归位推迟到 EVT_HW_ESTOP_OFF 触发
 *
 *   安全归位（do_safety_home）当前动作：
 *     brush_off()：停刷子 VFD 并重置接触器，确保刷子回零位，车辆可安全退出
 *     （龙门不移动，M8 场景下龙门不阻碍退车）
 */

#include "framework/application/orchestrators/emergency_handler.h"
#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include "framework/common/sw_error.h"

/* true = 物理急停按钮处于激活（断电）状态；安全归位须等释放后执行 */
static bool s_estop_active = false;

/* -------------------------------------------------------------------------
 * 安全归位：刷子回零位，使车辆可安全退出，完成后通知 device_fsm
 * ------------------------------------------------------------------------- */
static void do_safety_home(void)
{
    (void)brush_stop();    /* 停 VFD，接触器保持当前位置（刷子已停转） */
    (void)event_publish(EVT_SAFETY_HOME_DONE, (uint32_t)SW_OK);
    LOG_INFO("emergency_handler: safety home done");
}

/* -------------------------------------------------------------------------
 * 事件处理函数（在 event_dispatch_thread 上下文执行）
 * ------------------------------------------------------------------------- */

/* EVT_HW_ESTOP_ON：物理急停按下
 *   硬件断电已停止所有执行器；记录急停状态，安全归位等待释放 */
static void on_estop_on(const event_t *evt)
{
    (void)evt;
    s_estop_active = true;
    LOG_WARN("emergency_handler: ESTOP activated, safety home deferred until release");
}

/* EVT_HW_ESTOP_OFF：急停释放
 *   电源恢复，可以移动刷子；执行安全归位 */
static void on_estop_off(const event_t *evt)
{
    (void)evt;
    s_estop_active = false;
    LOG_INFO("emergency_handler: ESTOP released, executing safety home");
    do_safety_home();
}

/* EVT_SAFETY_LOCKOUT：报警级别的安全锁定
 *   中止洗车；若非急停中则立即执行安全归位，否则推迟到急停释放后 */
static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    wash_orchestrator_abort();

    if (s_estop_active)
    {
        /* 急停断电中，安全归位将在 on_estop_off 执行 */
        LOG_WARN("emergency_handler: LOCKOUT during ESTOP, wash aborted, home deferred");
        return;
    }

    LOG_WARN("emergency_handler: LOCKOUT, wash aborted, executing safety home");
    do_safety_home();
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */

sw_err_t emergency_handler_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_HW_ESTOP_ON,    on_estop_on      },
        { EVT_HW_ESTOP_OFF,   on_estop_off     },
        { EVT_SAFETY_LOCKOUT, on_safety_lockout },
    };

    return event_subscribe_table(s_subs,
                                 sizeof(s_subs) / sizeof(s_subs[0]));
}
