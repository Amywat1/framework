/**
 * @file    wiring.c
 * @brief   依赖注入实现（真机：linux_hw / generic HAL 适配器）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    此文件只做 port→adapter 注册，不执行任何硬件初始化。
 *          硬件初始化（hal_io.init / hal_vfd.init）由 bootstrap.c 负责，
 *          保证初始化顺序可控。
 *          CMake BUILD_SIM=ON 时使用 wiring_sim.c 替换本文件。
 */

#include "infrastructure/bootstrap/wiring.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * HAL 适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void hal_motor_generic_register(void);
extern void hal_sensor_generic_register(void);
extern void hal_io_linux_register(void);
extern void hal_vfd_linux_register(void);
extern void hal_voice_linux_register(void);
extern void hal_do_group_generic_register(void);

/* -------------------------------------------------------------------------
 * 存储适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void json_param_store_register(void);
extern void json_deploy_store_register(void);

/* -------------------------------------------------------------------------
 * 云端上报适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void aliyun_report_adapter_register(void);

/* -------------------------------------------------------------------------
 * 引擎 IO 后端注册函数声明
 * ------------------------------------------------------------------------- */
extern void engine_io_m8_register(void);

/* -------------------------------------------------------------------------
 * 命令桥接适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void command_bridge_register(void);

sw_err_t wiring(void)
{
    /* HAL port → 实现注册（io/vfd/voice: linux_hw；motor/sensor/do_group: generic） */
    hal_motor_generic_register();
    hal_sensor_generic_register();
    hal_io_linux_register();
    hal_vfd_linux_register();
    hal_voice_linux_register();
    hal_do_group_generic_register();

    /* 存储 port → JSON 文件实现 */
    json_param_store_register();
    json_deploy_store_register();

    /* 云端上报 port → 阿里云 MQTT 实现 */
    aliyun_report_adapter_register();

    /* 命令 port → event_bus 桥接 */
    command_bridge_register();

    /* 引擎 IO 后端：M8 机型桥接 engine IO 接口到 gantry/brush/water 设备驱动 */
    engine_io_m8_register();

    LOG_INFO("wiring: all adapters registered");
    return SW_OK;
}
