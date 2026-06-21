/**
 * @file    cli_adapter.cpp
 * @brief   CLI 命令域注册
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    注册 device、safety、param、diag 四个命令域。
 */

#include "adapters/ui/cli/cli_commands.h"
#include "cli/cli.h"
#include <cstring>

/* -------------------------------------------------------------------------
 * CLI 分发函数（使用 cli::get() 读取参数）
 * 命令格式：<domain> <subcmd> [p1] [p2]
 * ------------------------------------------------------------------------- */

static int device_debug(void)
{
    char *args[4] = { cli::get(0), cli::get(1), cli::get(2), cli::get(3) };
    if (args[0] == nullptr || strcmp(args[0], "device") != 0) { return 0; }
    return device_cmd_handler(args[1], args[2], args[3]);
}

static int safety_debug(void)
{
    char *args[3] = { cli::get(0), cli::get(1), cli::get(2) };
    if (args[0] == nullptr || strcmp(args[0], "safety") != 0) { return 0; }
    return safety_cmd_handler(args[1], args[2], nullptr);
}

static int param_debug(void)
{
    char *args[4] = { cli::get(0), cli::get(1), cli::get(2), cli::get(3) };
    if (args[0] == nullptr || strcmp(args[0], "param") != 0) { return 0; }
    return param_cmd_handler(args[1], args[2], args[3]);
}

static int diag_debug(void)
{
    char *args[4] = { cli::get(0), cli::get(1), cli::get(2), cli::get(3) };
    if (args[0] == nullptr || strcmp(args[0], "diag") != 0) { return 0; }
    return diag_cmd_handler(args[1], args[2], args[3]);
}

/* -------------------------------------------------------------------------
 * 初始化入口（由 bootstrap 或 app_main 调用）
 * ------------------------------------------------------------------------- */
extern "C" void cli_adapter_init(void)
{
    cli::init();
    cli::add(device_debug);
    cli::add(safety_debug);
    cli::add(param_debug);
    cli::add(diag_debug);
}
