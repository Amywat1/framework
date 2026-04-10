/**
 * @file    scenario_standard_wash.c
 * @brief   场景测试：标准洗车流程（完整事件驱动路径，sim HAL）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 使用 sim HAL 适配器（不依赖真实硬件）
 *          - 预设传感器状态使所有步骤在第一次轮询（50ms）后完成
 *          - 验证完整的事件驱动路径：
 *            EVT_CMD_ORDER → device_fsm → wash_orchestrator →
 *            step_engine → EVT_WASH_DONE → device_fsm IDLE
 *          - 需要 BUILD_SIM 环境（链接 sim_hw/ 实现）
 */

#include "core/event_bus/event_bus.h"
#include "core/scheduler/thread_registry.h"
#include "core/scheduler/scheduler.h"
#include "service/dev_ctx/dev_ctx.h"
#include "domain/safety/alarm_core.h"
#include "domain/safety/safety_fsm.h"
#include "domain/device/brush.h"
#include "domain/device/gantry.h"
#include "domain/device/top_lift.h"
#include "domain/device/water.h"
#include "domain/device/gate.h"
#include "domain/model/device_state.h"
#include "domain/model/wash_types.h"
#include "domain/process/step_engine.h"
#include "application/orchestrators/safety_supervisor.h"
#include "application/orchestrators/device_fsm.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "adapters/hal/sim_hw/hal_sensor_sim.h"
#include "common/event_types.h"
#include "common/time_util.h"
#include "config/threading/thread_config.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <stdbool.h>

/* sim HAL 注册函数（无专用头文件，使用 extern 声明）*/
extern void hal_motion_sim_register(void);
extern void hal_water_sim_register(void);
extern void hal_indicator_sim_register(void);

/* -------------------------------------------------------------------------
 * 场景内部线程入口（不依赖 bootstrap 中的局部静态函数）
 * ------------------------------------------------------------------------- */
static void *scenario_dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static void *scenario_io_poll_fn(void *arg)
{
    (void)arg;
    while (true)
    {
        alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
        usleep((unsigned long)ALARM_POLL_PERIOD_MS * 1000UL);
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * 辅助：等待 device_fsm 到达目标状态（最多 max_ms 毫秒）
 * ------------------------------------------------------------------------- */
static bool wait_for_state(dev_state_t target, unsigned max_ms)
{
    unsigned elapsed = 0U;

    while (elapsed < max_ms)
    {
        if (device_fsm_get_state() == target)
        {
            return true;
        }
        usleep(50U * 1000U);
        elapsed += 50U;
    }
    return (device_fsm_get_state() == target);
}

/* -------------------------------------------------------------------------
 * 一次性全局初始化（所有 TC 共享，只执行一次）
 * ------------------------------------------------------------------------- */
static void scenario_setup(void)
{
    /* 注册 sim HAL 适配器 */
    hal_motion_sim_register();
    hal_sensor_sim_register();
    hal_water_sim_register();
    hal_indicator_sim_register();

    /* 预设传感器：升降在下限位 + 前后限位均已触发
     * → 所有步骤在第一次退出条件轮询（50ms）即可退出 */
    hal_sensor_sim_set_lift_bottom(true);
    hal_sensor_sim_set_fwd_limit(true);
    hal_sensor_sim_set_rev_limit(true);

    /* 初始化各子系统（顺序与 bootstrap.c 保持一致）*/
    time_util_init();
    (void)event_bus_init();
    /* svc_param 未注册 param_store；svc_param_get_int 使用默认值，不影响场景 */
    (void)dev_ctx_init();
    (void)alarm_core_init();
    (void)safety_fsm_init();
    (void)brush_init();
    (void)gantry_init();
    (void)top_lift_init();
    (void)water_init();
    (void)gate_init();
    (void)safety_supervisor_init();
    (void)device_fsm_init();
    (void)wash_orchestrator_init();
    (void)step_engine_init();

    /* 注册并启动线程：event_dispatch + io_poll + wash_worker（已由 orchestrator 注册）*/
    (void)thread_register("event_dispatch", scenario_dispatch_fn,
                          SCHED_OTHER, 0, THD_EVENT_DISPATCH_STACK);
    (void)thread_register("io_poll",        scenario_io_poll_fn,
                          SCHED_OTHER, 0, THD_IO_POLL_STACK);
    (void)scheduler_start_all();

    /* 等待线程就绪 */
    usleep(200U * 1000U);
}

/* -------------------------------------------------------------------------
 * TC-1：标准洗正常完成（CMD_ORDER → RUN → 步骤结束 → IDLE）
 * ------------------------------------------------------------------------- */
static void tc1_normal_complete(void)
{
    printf("TC-1: standard wash completes normally\n");

    assert(device_fsm_get_state() == DEV_STATE_IDLE);

    /* 发布洗车命令（标准洗 = mode 0）*/
    (void)event_publish(EVT_CMD_ORDER, (uint32_t)WASH_MODE_STANDARD);

    /* 等待 FSM 进入 RUN */
    assert(wait_for_state(DEV_STATE_RUN, 500U));
    printf("  → RUN\n");

    /* 等待洗车结束（所有步骤 50ms/步 × 8 步 ≈ 400ms；余量 3s）*/
    assert(wait_for_state(DEV_STATE_IDLE, 3000U));
    printf("  → IDLE (wash done)\n");

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-2：洗车中途手动停止（STOP_WASH → s_manual_stop=true → IDLE 而非 FAULT）
 * ------------------------------------------------------------------------- */
static void tc2_manual_stop(void)
{
    printf("TC-2: manual stop during wash\n");

    assert(device_fsm_get_state() == DEV_STATE_IDLE);

    /* 启动洗车 */
    (void)event_publish(EVT_CMD_ORDER, (uint32_t)WASH_MODE_STANDARD);
    assert(wait_for_state(DEV_STATE_RUN, 500U));
    printf("  → RUN\n");

    /* 在洗车进行中发布手动停止 */
    usleep(80U * 1000U);
    (void)event_publish(EVT_CMD_STOP_WASH, 0U);

    /* 应回到 IDLE（不进入 FAULT）*/
    assert(wait_for_state(DEV_STATE_IDLE, 2000U));
    assert(device_fsm_get_state() != DEV_STATE_FAULT);
    printf("  → IDLE (manual stop, not FAULT)\n");

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-3：运营停止 / 恢复（STOP_OPERATION → STOP → RESUME_OPERATION → IDLE）
 * ------------------------------------------------------------------------- */
static void tc3_stop_resume_operation(void)
{
    printf("TC-3: stop/resume operation\n");

    assert(device_fsm_get_state() == DEV_STATE_IDLE);

    (void)event_publish(EVT_CMD_STOP_OPERATION, 0U);
    assert(wait_for_state(DEV_STATE_STOP, 200U));
    printf("  → STOP\n");

    (void)event_publish(EVT_CMD_RESUME_OPERATION, 0U);
    assert(wait_for_state(DEV_STATE_IDLE, 200U));
    printf("  → IDLE\n");

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== scenario_standard_wash ===\n");

    scenario_setup();

    tc1_normal_complete();
    tc2_manual_stop();
    tc3_stop_resume_operation();

    printf("=== ALL PASSED ===\n");
    return 0;
}
