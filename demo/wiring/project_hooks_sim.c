/**
 * @file    project_hooks_sim.c
 * @brief   Demo 仿真项目生命周期钩子
 *
 * @note    与产品项目同一张 15 钩子表，未用阶段填 project_hook_noop，不得留空。
 *          产品相对 Demo 通常还要在对应阶段做：
 *          configure/bind/init_hal  下发并绑定传感器、变频器、语音
 *          init_safety / assert_safe_outputs  上电安全态与 panic 切断
 *          configure_adapters       云端密钥等入站配置
 *          init_device              机构、方案会话、入场监督
 *          register_runtime_tasks   滤波、机构 tick、观测、上报
 *          start_runtime            等传感器就绪、启动无法纳入 scheduler 的线程
 *          真机与仿真拆两份 hooks，共享 domain 与 application。
 *          资产校验 asset_contract_validate 在有方案/云的项目于 validate 追加；
 *          Demo 为保持最小链接集不拉 wdf_asset_contract。
 */

#include "wiring/demo_defs.h"

#include "adapters/inbound/safety/estop_poll_thread.h"
#include "adapters/outbound/storage/json/json_deploy_store.h"
#include "adapters/outbound/storage/json/json_param_store.h"
#include "application/blackbox_sampler.h"
#include "application/bridges/estop_alarm_bridge.h"
#include "application/bridges/observation_event_bridge.h"
#include "application/pending_start.h"
#include "application/wash_quota.h"
#include "runtime/bootstrap/project_hooks.h"
#include "runtime/ports/port_contract.h"

extern sw_err_t demo_device_ops_register(void);
extern sw_err_t demo_alarm_catalog_load(void);

static sw_err_t configure_storage(void)
{
    sw_err_t r;

    r = json_param_store_configure(PARAM_STORE_JSON_FILE_PATH);
    if (r != SW_OK) {
        return r;
    }
    return json_deploy_store_configure(DEPLOY_STORE_JSON_FILE_PATH);
}

static sw_err_t bind_device(void)
{
    return demo_device_ops_register();
}

static sw_err_t init_device(void)
{
    sw_err_t ret;

    pending_start_init();
    ret = wash_quota_init();
    if (ret != SW_OK) {
        return ret;
    }
    /* 无云时视为可监控，避免剩余 0 拦住开洗；真机按连接边沿置位，连通不回满。 */
    wash_quota_set_monitorable(true);
    return SW_OK;
}

static sw_err_t bind_alarm_catalog(void)
{
    return demo_alarm_catalog_load();
}

static sw_err_t validate(void)
{
    return port_contract_validate(PORT_REQ_HAL_IO | PORT_REQ_HAL_VOICE | PORT_REQ_PARAM_STORE | PORT_REQ_DEPLOY_STORE
                                  | PORT_REQ_DEVICE_OPS | PORT_REQ_ALARM_BINDING | PORT_REQ_SAFETY
                                  | PORT_REQ_ENTRY_OCCUPANCY);
}

static sw_err_t init_adapters(void)
{
    sw_err_t ret;

    ret = estop_poll_thread_init(NULL);
    if (ret != SW_OK) {
        return ret;
    }
    ret = estop_alarm_bridge_init(DEMO_ALARM_ESTOP);
    if (ret != SW_OK) {
        return ret;
    }
    ret = observation_event_bridge_init();
    if (ret != SW_OK) {
        return ret;
    }
    return blackbox_sampler_start(10U, 5U, BLACKBOX_TRIG_ESTOP | BLACKBOX_TRIG_LOCKOUT);
}

static sw_err_t register_runtime_tasks(void)
{
    return pending_start_register_task();
}

static void assert_safe_outputs(void)
{
    /* 仿真无真实输出。产品须幂等切断到与 cutout 等价的安全态。 */
}

sw_err_t project_hooks_register(void)
{
    static const project_hooks_t s_hooks = {
        .configure_storage      = configure_storage,
        .configure_hal          = project_hook_noop,
        .bind_hal               = project_hook_noop,
        .init_hal               = project_hook_noop,
        .configure_safety       = project_hook_noop,
        .init_safety            = project_hook_noop,
        .configure_adapters     = project_hook_noop,
        .bind_device            = bind_device,
        .init_device            = init_device,
        .bind_alarm_catalog     = bind_alarm_catalog,
        .validate               = validate,
        .init_adapters          = init_adapters,
        .register_runtime_tasks = register_runtime_tasks,
        .start_runtime          = project_hook_noop,
        .assert_safe_outputs    = assert_safe_outputs,
    };
    return bootstrap_register_hooks(&s_hooks);
}
