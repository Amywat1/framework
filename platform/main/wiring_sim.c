/**
 * @file    wiring_sim.c
 * @brief   仿真构建的依赖接线实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    注册仿真 HAL、JSON 存储以及 BUILD_SIM 使用的命令桥接实现。
 */

#include "infrastructure/bootstrap/wiring.h"
#include "common/log.h"

/* sim HAL 适配器（io/vfd/motor：sim 专属；sensor/do_group：generic 共用） */
extern void hal_motor_sim_register(void);
extern void hal_sensor_generic_register(void);
extern void hal_io_sim_register(void);
extern void hal_vfd_sim_register(void);
extern void hal_do_group_generic_register(void);

/* 引擎 IO 后端（桥接 engine IO 接口到设备驱动 API） */
extern void engine_io_hal_register(void);
/* 方案加载器（JSON 格式实现注册到 engine_program_loader_port） */
extern void engine_program_json_register_loader(void);

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

    /* 引擎 IO 后端：桥接 engine IO 接口到 gantry/brush/water 设备驱动 */
    engine_io_hal_register();
    /* 方案加载器：JSON 格式 → engine_program_loader_port */
    engine_program_json_register_loader();

    /* 仿真不注册云端上报适配器（cloud_report_get_ops() 返回 NULL，
     * report_aggregator 会静默跳过上报）*/

    LOG_INFO("wiring_sim: all sim adapters registered");
    return SW_OK;
}
