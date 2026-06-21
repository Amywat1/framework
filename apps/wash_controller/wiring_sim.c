/**
 * @file    wiring_sim.c
 * @brief   仿真构建的依赖接线实现
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    注册仿真 HAL、JSON 存储以及 BUILD_SIM 使用的命令桥接实现。
 */

#include "core/bootstrap/wiring.h"
#include "common/log.h"

/* sim HAL 适配器（io/vfd/motor：sim 专属；sensor/do_group：generic 共用） */
extern void hal_motor_sim_register(void);
extern void hal_sensor_generic_register(void);
extern void hal_io_sim_register(void);
extern void hal_vfd_sim_register(void);
extern void hal_do_group_generic_register(void);

/* 存储适配器（与真机相同，使用相同 JSON 文件路径）*/
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

/* 命令桥接（与真机相同）*/
extern void command_bridge_register(void);

sw_err_t wiring(void)
{
    /* HAL port 注册（io/vfd/motor: sim_hw；sensor/do_group: generic） */
    hal_motor_sim_register();
    hal_sensor_generic_register();
    hal_io_sim_register();
    hal_vfd_sim_register();
    hal_do_group_generic_register();

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
