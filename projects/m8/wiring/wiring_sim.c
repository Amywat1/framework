/**
 * @file    wiring_sim.c
 * @brief   仿真构建的依赖接线实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/runtime/bootstrap/wiring.h"
#include "framework/common/log.h"

extern void hal_sensor_filter_register(void);
extern void hal_io_sim_register(void);
extern void hal_vfd_sim_register(void);
extern void hal_voice_sim_register(void);
extern void engine_io_m8_register(void);
extern void engine_program_json_register_loader(void);
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

sw_err_t wiring(void)
{
    hal_sensor_filter_register();
    hal_io_sim_register();
    hal_vfd_sim_register();
    hal_voice_sim_register();

    json_param_store_register();
    json_deploy_store_register();

    /* 命令 port 由 bootstrap command_gateway_init() 注册（仿真） */
    engine_io_m8_register();
    engine_program_json_register_loader();

    LOG_INFO("wiring_sim: all sim adapters registered");
    return SW_OK;
}
