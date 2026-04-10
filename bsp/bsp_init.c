/**
 * @file    bsp_init.c
 * @brief   系统初始化序列实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "bsp_init.h"
#include "bsp_hal.h"
#include "bsp_alarm.h"
#include "service/svc_alarm.h"
#include "service/svc_param.h"
#include "common/log.h"
#include "config/machine_config.h"
#include "io_exp/demo.h"
#include <unistd.h>

sw_err_t bsp_system_init(void)
{
    sw_err_t ret;

    /* ------------------------------------------------------------------ */
    /* 步骤1：初始化 IO 子板（CAN 总线）                                    */
    /* ------------------------------------------------------------------ */
    if (io_init(CFG_IO_CAN_BUS, CFG_IO_CAN_BAUD,
                CFG_IO_SELF_NODE, CFG_IO_BOARD_COUNT) != 0) {
        LOG_ERROR("bsp_system_init: IO board init failed");
        return SW_ERR_HW;
    }

    /* 等待 IO 子板上电稳定（固定 2s，不可省略）*/
    LOG_INFO("Waiting for IO boards to stabilize...");
    sleep(2);

    /* ------------------------------------------------------------------ */
    /* 步骤2：初始化所有驱动（VFD、步进）                                   */
    /* ------------------------------------------------------------------ */
    ret = hal_init();
    if (ret != SW_OK) {
        LOG_ERROR("bsp_system_init: hal_init failed");
        return ret;
    }

    /* ------------------------------------------------------------------ */
    /* 步骤3：初始化报警引擎                                                */
    /* ------------------------------------------------------------------ */
    ret = svc_alarm_init();
    if (ret != SW_OK) {
        LOG_ERROR("bsp_system_init: svc_alarm_init failed");
        return ret;
    }

    /* ------------------------------------------------------------------ */
    /* 步骤4：注册 M8 报警回调（必须在 hal_init 之后）                     */
    /* ------------------------------------------------------------------ */
    ret = bsp_alarm_init();
    if (ret != SW_OK) {
        LOG_ERROR("bsp_system_init: bsp_alarm_init failed");
        return ret;
    }

    /* ------------------------------------------------------------------ */
    /* 步骤5：加载运行参数                                                  */
    /* ------------------------------------------------------------------ */
    ret = svc_param_init();
    if (ret != SW_OK) {
        /* 参数文件不存在时使用默认值，非致命错误 */
        LOG_WARN("bsp_system_init: param file missing, using defaults");
    }

    LOG_INFO("bsp_system_init complete");
    return SW_OK;
}
