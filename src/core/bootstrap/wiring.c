/**
 * @file    wiring.c
 * @brief   依赖注入实现（真机：linux_hw HAL 适配器）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    此文件仅在 BUILD_SIM=OFF 时编译。
 *          CMake BUILD_SIM=ON 时使用 wiring_sim.c 替换。
 */

#include "core/bootstrap/wiring.h"
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * HAL 适配器注册函数声明（各 .c 文件已实现）
 * ------------------------------------------------------------------------- */
extern void hal_motion_linux_register(void);
extern void hal_sensor_linux_register(void);
extern void hal_io_linux_register(void);
extern void hal_water_linux_register(void);
extern void hal_indicator_linux_register(void);

/*
 * param_store / deploy_store / cloud_report 适配器注册由 Phase 6 补充。
 * 现阶段 svc_param 直接使用 cJSON，cloud 适配器尚未实现，保持 NULL（优雅降级）。
 */

sw_err_t wiring(void)
{
    sw_err_t ret;

    /* 1. 初始化 M8 硬件上下文（VFD 实例、步进驱动）*/
    ret = m8_linux_hw_init();
    if (ret != SW_OK)
    {
        LOG_ERROR("wiring: m8_linux_hw_init failed ret=%d", (int)ret);
        return ret;
    }

    /* 2. 注册 HAL 适配器（port → linux_hw 实现）*/
    hal_motion_linux_register();
    hal_sensor_linux_register();
    hal_io_linux_register();
    hal_water_linux_register();
    hal_indicator_linux_register();

    LOG_INFO("wiring: all HAL adapters registered (linux_hw)");
    return SW_OK;
}
