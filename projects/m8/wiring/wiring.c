/**
 * @file    wiring.c
 * @brief   依赖注入实现（真机：providers/snack / generic HAL 适配器）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    此文件只做 port→adapter 注册，不执行任何硬件初始化。
 *          硬件初始化（hal_io.init / hal_vfd.init）由 bootstrap.c 负责，
 *          保证初始化顺序可控。
 *          CMake BUILD_SIM=ON 时使用 wiring_sim.c 替换本文件。
 */

#include "framework/runtime/bootstrap/wiring.h"
#include "framework/common/log.h"

/* -------------------------------------------------------------------------
 * HAL 适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void hal_sensor_filter_register(void);
extern void m8_io_adapter_register(void);
extern void snack_vfd_backend_register(void);
extern void snack_voice_adapter_register(void);
extern void hal_do_group_mapper_register(void);

/* -------------------------------------------------------------------------
 * 日志 sink 注册函数声明
 * ------------------------------------------------------------------------- */
extern void snack_log_sink_register(void);

/* -------------------------------------------------------------------------
 * 存储适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

#include "projects/m8/adapters/cloud/m8_point_table.h"
#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_report_adapter.h"

/* -------------------------------------------------------------------------
 * 引擎 IO 后端注册函数声明
 * ------------------------------------------------------------------------- */
extern void engine_io_m8_register(void);

#include "framework/services/command_router/command_handler.h"

sw_err_t wiring(void)
{
    /* 尽早注册日志 sink，确保本函数及后续所有日志都经 Snack 输出 */
    snack_log_sink_register();

    /* HAL port → 实现注册（io/vfd/voice: providers/snack；sensor/do_group: generic） */
    hal_sensor_filter_register();
    m8_io_adapter_register();
    snack_vfd_backend_register();
    snack_voice_adapter_register();
    hal_do_group_mapper_register();

    /* 存储 port → JSON 文件实现 */
    json_param_store_register();
    json_deploy_store_register();

    /* 云端上报 port → Snack MQTT 云实现（注入 M8 上报 JSON 构建器）*/
    snack_cloud_report_adapter_register(m8_cloud_report_json);

    /* 命令 port → command_handler（校验 + event_bus 路由）*/
    command_handler_register();

    /* 引擎 IO 后端：M8 机型桥接 engine IO 接口到 gantry/brush/water 设备驱动 */
    engine_io_m8_register();

    LOG_INFO("wiring: all adapters registered");
    return SW_OK;
}
