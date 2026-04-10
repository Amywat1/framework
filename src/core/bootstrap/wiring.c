/**
 * @file    wiring.c
 * @brief   依赖注入实现（真机：linux_hw HAL 适配器）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    此文件只做 port→adapter 注册，不执行任何硬件初始化。
 *          硬件初始化（drv_io_init / m8_linux_hw_init）由 bootstrap.c 负责，
 *          保证初始化顺序可控。
 *          CMake BUILD_SIM=ON 时使用 wiring_sim.c 替换本文件。
 */

#include "core/bootstrap/wiring.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * HAL 适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void hal_motion_linux_register(void);
extern void hal_sensor_linux_register(void);
extern void hal_io_linux_register(void);
extern void hal_water_linux_register(void);
extern void hal_indicator_linux_register(void);

/* -------------------------------------------------------------------------
 * 存储适配器注册函数声明
 * ------------------------------------------------------------------------- */
extern void json_param_store_register(void);

sw_err_t wiring(void)
{
    /* HAL port → linux_hw 实现 */
    hal_motion_linux_register();
    hal_sensor_linux_register();
    hal_io_linux_register();
    hal_water_linux_register();
    hal_indicator_linux_register();

    /* 存储 port → JSON 文件实现 */
    json_param_store_register();

    LOG_INFO("wiring: all adapters registered");
    return SW_OK;
}
