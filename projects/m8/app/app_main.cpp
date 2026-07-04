/**
 * @file    app_main.cpp
 * @brief   应用入口回调（snack 运行时调用的 app_main，而非 main）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    通过运行时适配器接入 snack 运行时相关能力，
 *          再调用 bootstrap_run() 完成模块初始化与工作线程启动。
 *          CLI 注册由 bootstrap 统一完成。
 */

#include "framework/runtime/bootstrap/bootstrap.h"
#include "framework/common/sw_version.h"
#include "framework/common/log.h"
#include "framework/adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"
#include "projects/m8/config/m8_machine_config.h"
#include "projects/m8/adapters/m8_runtime_adapter.h"
#include "projects/m8/adapters/runtime/snack/snack_log.h"
#include <unistd.h>

static const char *APP_NAME    = SW_PRODUCT_NAME;
static const char *APP_VERSION = SW_VERSION_STR;

void app_info_set(char **name, char **version)
{
    *name    = (char *)APP_NAME;
    *version = (char *)APP_VERSION;
}

/* -------------------------------------------------------------------------
 * 应用入口（snack 框架回调）
 * ------------------------------------------------------------------------- */
int app_main(int, char **)
{
    int io_ret = 0;

    set_log_level(6);
    set_app_version((char *)APP_NAME, (char *)APP_VERSION);
    m8_runtime_adapter_init();

    /* 先初始化 io_exp SDK 的 CAN 总线访问，再进入后续模块初始化 */
    io_ret = io_exp_driver_sdk_init(CFG_IO_CAN_BUS, CFG_IO_CAN_BAUD, CFG_IO_SELF_NODE, CFG_IO_BOARD_COUNT);
    if (io_ret != SW_OK)
    {
        LOG_ERROR("app_main: io_init failed ret=%d", io_ret);
        return -1;
    }

    /* SDK 初始化完成后等待子板上电稳定 */
    sleep(2);

    if (bootstrap_run() != SW_OK)
    {
        LOG_ERROR("app_main: bootstrap_run failed, abort");
        return -1;
    }

    LOG_INFO("%s %s started", APP_NAME, APP_VERSION);

    while (true)
    {
        sleep(10);
    }

    return 0;
}
