/**
 * @file    wiring_sim.c
 * @brief   Demo 仿真依赖接线
 *
 * @note    wiring() 只 register provider，路径与实例绑定放进 hooks。
 *          产品通常拆 sim / 真机两份：本文件对应仿真；真机把 *_sim 换成现场
 *          provider，并注册云连接。有模拟量或变频器时再挂 sensor / vfd；
 *          有方案引擎时再挂 engine_program_json_register_loader。
 */

#include "adapters/outbound/safety/sim/safety_sim.h"
#include "common/log.h"
#include "runtime/bootstrap/wiring.h"

extern void hal_io_sim_register(void);
extern void hal_voice_sim_register(void);
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

sw_err_t wiring(void)
{
    sw_err_t ret;

    hal_io_sim_register();
    hal_voice_sim_register();
    json_param_store_register();
    json_deploy_store_register();

    /* 安全端口改为注册表式绑定，仿真实现须显式注册；
     * 漏注册会被 project_validate 的 PORT_REQ_SAFETY 校验拦住。 */
    ret = safety_sim_register();
    if (ret != SW_OK) {
        LOG_ERROR("Demo wiring: safety_sim_register failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("Demo wiring: sim adapters registered");
    return SW_OK;
}
