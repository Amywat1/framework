/**
 * @file    wiring_sim.c
 * @brief   依赖注入实现（PC 仿真：sim_hw HAL 适配器）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    BUILD_SIM=ON 时替换 wiring.c。
 *          只注册仿真 HAL、JSON 存储和命令桥接，不注册真机硬件或云端适配器。
 */

#include "core/bootstrap/wiring.h"
#include "common/log.h"

/* sim HAL 适配器 */
extern void hal_motion_sim_register(void);
extern void hal_sensor_sim_register(void);
extern void hal_io_sim_register(void);
extern void hal_water_sim_register(void);
extern void hal_indicator_sim_register(void);

/* 存储适配器（与真机相同，使用相同 JSON 文件路径）*/
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

/* 命令桥接（与真机相同）*/
extern void command_bridge_register(void);

sw_err_t wiring(void)
{
    /* sim HAL port → sim_hw 实现 */
    hal_motion_sim_register();
    hal_sensor_sim_register();
    hal_io_sim_register();
    hal_water_sim_register();
    hal_indicator_sim_register();

    /* 存储 port → JSON 文件实现（仿真也使用持久化参数）*/
    json_param_store_register();
    json_deploy_store_register();

    /* 命令 port → event_bus 桥接 */
    command_bridge_register();

    /* 仿真不注册云端上报适配器（cloud_report_get_ops() 返回 NULL，
     * report_aggregator 会静默跳过上报）*/

    LOG_INFO("wiring_sim: all sim adapters registered");
    return SW_OK;
}
