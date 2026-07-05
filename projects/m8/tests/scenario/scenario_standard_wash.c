/**
 * @file    scenario_standard_wash.c
 * @brief   场景测试：标准洗车流程（完整事件驱动路径，sim HAL）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 使用 sim HAL 适配器（不依赖真实硬件）
 *          - 预设传感器状态使所有步骤在第一次轮询（50ms）后完成
 *          - 验证完整的事件驱动路径：
 *            EVT_CMD_ORDER → device_fsm → wash_orchestrator →
 *            wash_exec_step → EVT_WASH_DONE → device_fsm IDLE
 *          - 需要 BUILD_SIM 环境（链接 sim/ 实现）
 */

#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/runtime/scheduler/scheduler.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/water.h"
#include "framework/domain/device_control/model/device_state.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/domain/wash/model/engine_model.h"  /* engine_direction_t，供下方 extern 使用 */
#include "framework/application/orchestrators/emergency_handler.h"
#include "framework/application/orchestrators/device_fsm.h"
#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/bindings/m8_signal_sim.h"
#include "projects/m8/bindings/m8_motor_domains_setup.h"
#include "projects/m8/bindings/m8_motor_exec.h"
#include "projects/m8/bindings/m8_vfd_tick.h"
#include "projects/m8/bindings/m8_water_setup.h"
#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/common/event_types.h"
#include "framework/common/time_util.h"
#include "framework/runtime/config/thread_config.h"
#include <assert.h>

/* IO 轮询线程参数（场景测试本地使用，不依赖全局 thread_config）*/
#define SCENARIO_IO_POLL_PERIOD_MS  20U
#define SCENARIO_IO_POLL_STACK      (16U * 1024U)
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <stdbool.h>

/* sim HAL 注册函数（无专用头文件，使用 extern 声明）*/
extern void hal_motor_sim_register(void);
extern void hal_io_sim_register(void);
extern void hal_vfd_sim_register(void);
extern void hal_do_group_mapper_register(void);
/* 引擎 IO 后端（M8 机型：桥接 engine IO 接口到设备驱动 API） */
extern void engine_io_m8_register(void);
/* 方案加载器（JSON 格式实现注册到 engine_program_loader_port） */
extern void engine_program_json_register_loader(void);
/* 测试辅助：读取当前引擎阶段行进方向，不在公开头文件中声明 */
extern engine_direction_t wash_orchestrator_current_direction(void);

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
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();

    (void)arg;
    while (true)
    {
        if ((sensor != NULL) && (sensor->tick != NULL))
        {
            sensor->tick();
        }
        usleep((unsigned long)SCENARIO_IO_POLL_PERIOD_MS * 1000UL);
    }
    return NULL;
}

/**
 * @brief  按引擎当前阶段行进方向注入限位信号，避免双限位同时触发组合报警
 */
static void *scenario_limit_inject_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        engine_direction_t dir = wash_orchestrator_current_direction();

        switch (dir)
        {
            case ENGINE_DIR_FORWARD:
                /* 前进时设前限位，保留后限位供下一阶段 entry_guard 使用 */
                m8_signal_sim_set_fwd_limit(true);
                break;

            case ENGINE_DIR_BACKWARD:
                /* 后退时设后限位，保留前限位供下一阶段 entry_guard 使用 */
                m8_signal_sim_set_rev_limit(true);
                break;

            default:
                /* 非运动阶段：保持当前限位状态不变 */
                break;
        }

        usleep(10U * 1000U);
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
        if (dev_ctx_get_device_state() == target)
        {
            return true;
        }
        usleep(50U * 1000U);
        elapsed += 50U;
    }
    return (dev_ctx_get_device_state() == target);
}

/* -------------------------------------------------------------------------
 * 一次性全局初始化（所有 TC 共享，只执行一次）
 * ------------------------------------------------------------------------- */
static void scenario_setup(void)
{
    /* 注册 sim HAL 适配器 */
    hal_io_sim_register();
    hal_vfd_sim_register();
    hal_motor_sim_register();
    hal_sensor_filter_register();
    hal_do_group_mapper_register();
    /* 引擎 IO 后端：M8 机型桥接到 gantry/brush/water 设备驱动 */
    engine_io_m8_register();
    /* 方案加载器：JSON 格式 → engine_program_loader_port */
    engine_program_json_register_loader();
    {
        const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

        if ((vfd != NULL) && (vfd->init != NULL))
        {
            (void)vfd->init();
        }
    }
    /* 初始化各子系统（顺序与 bootstrap.c 保持一致）*/
    time_util_init();
    (void)event_bus_init();
    (void)dev_ctx_init();
    (void)m8_sensor_setup();
    m8_signal_sim_reset_all();

    /* 预设传感器：龙门在后限位（归位位置），升降在上限位，后轮锁在原点 */
    m8_signal_sim_set_lift_bottom(true);
    m8_signal_sim_set_rev_limit(true);         /* 龙门初始归位 */
    m8_signal_sim_set_lift_top(true);          /* 升降初始在上限位，跳过 homing 中的升降步骤 */
    m8_signal_sim_set_rear_lock_home(true);    /* 后轮锁初始在原点，满足 homing 退出条件 */
    (void)m8_motor_exec_init();
    (void)m8_motor_domains_setup_mask(M8_DOMAIN_BRUSH | M8_DOMAIN_GANTRY);
    (void)m8_water_setup();
    (void)m8_motor_exec_start();
    (void)m8_vfd_tick_register_task();
    (void)emergency_handler_init();
    (void)device_fsm_init();
    (void)wash_orchestrator_init();

    /* 注册并启动线程：event_dispatch + io_poll + wash_worker（已由 orchestrator 注册）*/
    (void)thread_register("event_dispatch", scenario_dispatch_fn,
                          SCHED_OTHER, 0, THD_EVENT_DISPATCH_STACK);
    (void)thread_register("io_poll",        scenario_io_poll_fn,
                          SCHED_OTHER, 0, SCENARIO_IO_POLL_STACK);
    (void)thread_register("limit_inject", scenario_limit_inject_fn,
                          SCHED_OTHER, 0, SCENARIO_IO_POLL_STACK);
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

    assert(dev_ctx_get_device_state() == DEV_STATE_IDLE);

    /* 发布洗车命令（标准洗 = mode 0）*/
    (void)event_publish(EVT_CMD_ORDER, (uint32_t)WASH_MODE_STANDARD);

    /* 等待 FSM 进入 RUN */
    assert(wait_for_state(DEV_STATE_RUNNING, 500U));
    printf("  → RUN\n");

    /* 等待洗车结束（prepare 2s + 7 个运动阶段 × ~100ms；余量 5s）*/
    assert(wait_for_state(DEV_STATE_IDLE, 5000U));
    printf("  → IDLE (wash done)\n");

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-2：洗车中途手动停止（STOP_WASH → s_manual_stop=true → IDLE 而非 FAULT）
 * ------------------------------------------------------------------------- */
static void tc2_manual_stop(void)
{
    printf("TC-2: manual stop during wash\n");

    assert(dev_ctx_get_device_state() == DEV_STATE_IDLE);

    /* 启动洗车 */
    (void)event_publish(EVT_CMD_ORDER, (uint32_t)WASH_MODE_STANDARD);
    assert(wait_for_state(DEV_STATE_RUNNING, 500U));
    printf("  → RUN\n");

    /* 在洗车进行中发布手动停止 */
    usleep(80U * 1000U);
    (void)event_publish(EVT_CMD_STOP_WASH, 0U);

    /* 应回到 IDLE（不进入 FAULT）*/
    assert(wait_for_state(DEV_STATE_IDLE, 2000U));
    assert(dev_ctx_get_device_state() != DEV_STATE_FAULT);
    printf("  → IDLE (manual stop, not FAULT)\n");

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-3：运营停止 / 恢复（STOP_OPERATION → STOP → RESUME_OPERATION → IDLE）
 * ------------------------------------------------------------------------- */
static void tc3_stop_resume_operation(void)
{
    printf("TC-3: stop/resume operation\n");

    assert(dev_ctx_get_device_state() == DEV_STATE_IDLE);

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
