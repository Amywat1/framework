/**
 * @file    wiring_sim.c
 * @brief   Demo 示例的依赖接线实现
 */

#include "framework/runtime/bootstrap/wiring.h"
#include "framework/common/log.h"

extern void hal_io_sim_register(void);
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

sw_err_t wiring(void)
{
    hal_io_sim_register();
    json_param_store_register();
    json_deploy_store_register();

    LOG_INFO("Demo wiring: sim adapters registered");
    return SW_OK;
}
