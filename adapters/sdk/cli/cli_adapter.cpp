/**
 * @file    cli_adapter.cpp
 * @brief   CLI SDK（cli/cli.h）封装层
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅封装 SDK 原语（init/add/get），不含任何机型命令逻辑。
 *          M8 机型命令注册见 adapters/machine/m8/m8_cli_setup.c。
 */

#include "adapters/sdk/cli/cli_adapter.h"
#include "cli/cli.h"

extern "C" {

void cli_adapter_init(void)
{
    cli::init();
}

void cli_adapter_add(int (*cmd_fn)(void))
{
    cli::add(cmd_fn);
}

char *cli_adapter_get(int idx)
{
    return cli::get(idx);
}

} /* extern "C" */
