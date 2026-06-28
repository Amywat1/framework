/**
 * @file    m8_cli_setup.c
 * @brief   M8 机型 CLI 命令域注册
 * @author  HUWANGWEI
 * @date    2026-06-23
 *
 * @note    命令格式：<domain> <subcmd> [p1] [p2]
 *          各域处理器实现见 adapters/machine/m8/m8_cli_commands.c。
 */

#include "machines/m8/adapters/cli/m8_cli_setup.h"
#include "adapters/sdk/cli/cli_adapter.h"
#include "machines/m8/adapters/cli/m8_cli_commands.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * CLI 分发函数：读取参数后按域名路由到具体处理器
 * ------------------------------------------------------------------------- */

static int device_debug(void)
{
    char *args[4] = { cli_adapter_get(0), cli_adapter_get(1),
                      cli_adapter_get(2), cli_adapter_get(3) };
    if (args[0] == NULL || strcmp(args[0], "device") != 0) { return 0; }
    return device_cmd_handler(args[1], args[2], args[3]);
}

static int safety_debug(void)
{
    char *args[3] = { cli_adapter_get(0), cli_adapter_get(1), cli_adapter_get(2) };
    if (args[0] == NULL || strcmp(args[0], "safety") != 0) { return 0; }
    return safety_cmd_handler(args[1], args[2], NULL);
}

static int param_debug(void)
{
    char *args[4] = { cli_adapter_get(0), cli_adapter_get(1),
                      cli_adapter_get(2), cli_adapter_get(3) };
    if (args[0] == NULL || strcmp(args[0], "param") != 0) { return 0; }
    return param_cmd_handler(args[1], args[2], args[3]);
}

static int diag_debug(void)
{
    char *args[4] = { cli_adapter_get(0), cli_adapter_get(1),
                      cli_adapter_get(2), cli_adapter_get(3) };
    if (args[0] == NULL || strcmp(args[0], "diag") != 0) { return 0; }
    return diag_cmd_handler(args[1], args[2], args[3]);
}

/* -------------------------------------------------------------------------
 * 初始化入口（由 bootstrap 调用）
 * ------------------------------------------------------------------------- */
void m8_cli_setup(void)
{
    cli_adapter_init();
    cli_adapter_add(device_debug);
    cli_adapter_add(safety_debug);
    cli_adapter_add(param_debug);
    cli_adapter_add(diag_debug);
}
