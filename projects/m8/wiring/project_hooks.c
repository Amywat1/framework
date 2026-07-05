/**
 * @file    project_hooks.c
 * @brief   M8 真机项目生命周期钩子实现。
 * @author  HUWANGWEI
 * @date    2026-07-04
 *
 * @note    CMake BUILD_SIM=ON 时使用 project_hooks_sim.c 替换本文件。
 */

#include "framework/runtime/bootstrap/project_hooks.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_poll.h"
#include "projects/m8/adapters/alarm/m8_alarm_adapt.h"
#include "projects/m8/adapters/alarm/m8_alarm_init.h"
#include "projects/m8/adapters/alarm/m8_comm_watchdog.h"
#include "projects/m8/adapters/cli/m8_cli_setup.h"
#include "projects/m8/adapters/cloud/m8_tsl_table.h"
#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_adapter.h"
#include "projects/m8/bindings/m8_boot_profile.h"
#include "projects/m8/bindings/m8_machine_setup.h"
#include "projects/m8/bindings/m8_motor_exec.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/bindings/m8_vfd_setup.h"
#include "projects/m8/bindings/m8_vfd_tick.h"
#include "projects/m8/bindings/m8_voice_setup.h"

sw_err_t project_hal_extra_setup(void)
{
    sw_err_t r;

    r = m8_vfd_setup();
    if (r != SW_OK)
    {
        return r;
    }
    return m8_voice_setup();
}

sw_err_t project_safety_init(void)
{
    sw_err_t r;

    r = m8_boot_profile_init();
    if (r != SW_OK)
    {
        return r;
    }
    r = m8_sensor_setup();
    if (r != SW_OK)
    {
        return r;
    }
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
    if (snack_cloud_command_adapter_init(m8_tsl_command_dispatch))
    {
        (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
    }
    m8_cli_setup();
    return SW_OK;
}

sw_err_t project_start_threads(void)
{
    sw_err_t r;

    /* wiring() 已选择 hal_sensor_filter 作为 hal_sensor 适配器，
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
    r = m8_vfd_tick_register_task();
    if (r != SW_OK)
    {
        return r;
    }
    return m8_motor_exec_start();
}

void project_assert_safe_outputs(void)
{
    m8_assert_safe_outputs();
}
