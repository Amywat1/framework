/**
 * @file    wiring.c
 * @brief   依赖注入实现（真机：providers/snack / generic HAL 适配器）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/runtime/bootstrap/wiring.h"
#include "framework/common/log.h"

extern void hal_sensor_filter_register(void);
extern void m8_io_adapter_register(void);
extern void snack_vfd_backend_register(void);
extern void snack_voice_adapter_register(void);
extern void snack_log_sink_register(void);
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);
extern void engine_io_m8_register(void);

#include "projects/m8/adapters/cloud/m8_cloud_register.h"
#include "framework/adapters/inbound/cloud/providers/snack/snack_cloud_command_adapter.h"
#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_report_adapter.h"
#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_link_adapter.h"

sw_err_t wiring(void)
{
    sw_err_t ret;

    snack_log_sink_register();

    hal_sensor_filter_register();
    m8_io_adapter_register();
    snack_vfd_backend_register();
    snack_voice_adapter_register();

    json_param_store_register();
    json_deploy_store_register();

    snack_cloud_link_adapter_register();
    snack_cloud_report_adapter_register();
    ret = snack_cloud_command_adapter_register();
    if (ret != SW_OK)
    {
        return ret;
    }
    ret = m8_cloud_register();
    if (ret != SW_OK)
    {
        return ret;
    }

    /* 命令 port 由 bootstrap command_gateway_init() 注册 */
    engine_io_m8_register();

    LOG_INFO("wiring: all adapters registered");
    return SW_OK;
}
