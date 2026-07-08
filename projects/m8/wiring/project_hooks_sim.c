/**
 * @file    project_hooks_sim.c
 * @brief   M8 仿真构建的项目生命周期钩子实现。
 * @author  HUWANGWEI
 * @date    2026-07-04
 *
 * @note    真机构建使用 project_hooks.c 替换本文件。
 *          仿真不涉及真实硬件安全输出/云端接入/VFD-语音参数下发，
 *          对应钩子为空实现。
 */

#include "framework/runtime/bootstrap/project_hooks.h"
#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "framework/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"
#include "projects/m8/adapters/alarm/m8_alarm_adapt.h"
#include "projects/m8/adapters/alarm/m8_alarm_init.h"
#include "projects/m8/adapters/alarm/m8_comm_watchdog.h"
#include "projects/m8/bindings/m8_machine_setup.h"
#include "projects/m8/bindings/m8_motor_exec.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/bindings/m8_signal_sim.h"

sw_err_t project_report_scheduler_init(void)
{
    return SW_OK;
}

sw_err_t project_hal_extra_setup(void)
{
    return SW_OK;
}

sw_err_t project_safety_init(void)
{
    sw_err_t r;

    r = m8_sensor_setup();
    if (r != SW_OK)
    {
        return r;
    }
    m8_signal_sim_reset_all();
    return m8_sensor_warmup();
}

sw_err_t project_machine_setup(void)
{
    return m8_machine_setup();
}

sw_err_t project_alarm_catalog_init(void)
{
    sw_err_t r;

    r = m8_alarm_init();
    if (r != SW_OK)
    {
        return r;
    }
    r = m8_alarm_adapt_init();
    if (r != SW_OK)
    {
        return r;
    }
    return m8_comm_watchdog_init();
}

sw_err_t project_adapters_init(void)
{
    /* 仿真不注册云端/CLI 适配器 */
    return SW_OK;
}

sw_err_t project_start_threads(void)
{
    sw_err_t r;

    /* wiring_sim() 已选择 hal_sensor_filter 作为 hal_sensor 适配器，
     * 这里注册其驱动所需的通用滤波周期任务 */
    r = hal_sensor_poll_register_task();
    if (r != SW_OK)
    {
        return r;
    }
    r = m8_alarm_adapt_poll_start();
    if (r != SW_OK)
    {
        return r;
    }
    r = hal_vfd_manager_poll_register_task();
    if (r != SW_OK)
    {
        return r;
    }
    return m8_motor_exec_start();
}

void project_assert_safe_outputs(void)
{
    /* 仿真无真实硬件输出，无需动作 */
}
