/**
 * @file    wiring_sim.c
 * @brief   Demo 项目仿真依赖接线
 */

#include "common/log.h"
#include "runtime/bootstrap/wiring.h"

extern void hal_io_sim_register(void);
extern void hal_voice_sim_register(void);
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

sw_err_t wiring(void)
{
    hal_io_sim_register();
    hal_voice_sim_register();
    json_param_store_register();
    json_deploy_store_register();

    LOG_INFO("Demo wiring: sim adapters registered");
    return SW_OK;
}
