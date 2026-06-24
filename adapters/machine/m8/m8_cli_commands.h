/**
 * @file    m8_cli_commands.h
 * @brief   M8 机型 CLI 命令处理函数声明
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef ADAPTERS_MACHINE_M8_CLI_COMMANDS_H
#define ADAPTERS_MACHINE_M8_CLI_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief device 命令域：status / order [mode] / stop / stop-op / resume / reset / home */
int device_cmd_handler(char *subcmd, char *p1, char *p2);

/** @brief safety 命令域：status / reset */
int safety_cmd_handler(char *subcmd, char *p1, char *p2);

/** @brief param 命令域：get <key> / set <key> <val> / save */
int param_cmd_handler(char *subcmd, char *p1, char *p2);

/** @brief diag 命令域：do <DO_NAME> <0|1> / di <DI_NAME> / io [BOARD_ID] / state */
int diag_cmd_handler(char *subcmd, char *p1, char *p2);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_CLI_COMMANDS_H */
