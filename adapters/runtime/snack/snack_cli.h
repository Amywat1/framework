/**
 * @file    snack_cli.h
 * @brief   snack CLI SDK（cli/cli.h）封装接口
 * @author  HUWANGWEI
 * @date    2026-06-23
 *
 * @note    封装 cli::init/add/get 三个 SDK 原语，供机型 setup 文件使用。
 *          可跨项目复用，不含任何机型命令逻辑。
 */

#ifndef PROJECTS_M8_ADAPTERS_RUNTIME_SNACK_CLI_H
#define PROJECTS_M8_ADAPTERS_RUNTIME_SNACK_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 CLI SDK */
void cli_adapter_init(void);

/**
 * @brief 注册一个命令处理函数
 * @param cmd_fn  返回 int、无参数的命令函数指针
 */
void cli_adapter_add(int (*cmd_fn)(void));

/**
 * @brief 读取当前命令的第 idx 个参数（SDK cli::get 的 C 封装）
 * @param idx  参数下标，从 0 开始
 * @return     参数字符串指针，超出范围时返回 NULL
 */
char *cli_adapter_get(int idx);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_RUNTIME_SNACK_CLI_H */
