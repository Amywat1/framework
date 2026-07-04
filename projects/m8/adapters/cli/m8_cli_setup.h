/**
 * @file    m8_cli_setup.h
 * @brief   M8 机型 CLI 命令域注册接口
 * @author  HUWANGWEI
 * @date    2026-06-23
 *
 * @note    注册 device、safety、param、diag 四个命令域，供 bootstrap 调用。
 */

#ifndef ADAPTERS_MACHINE_M8_CLI_SETUP_H
#define ADAPTERS_MACHINE_M8_CLI_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 CLI 并注册 M8 机型所有命令域 */
void m8_cli_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_CLI_SETUP_H */
