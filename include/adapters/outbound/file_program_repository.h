#ifndef ADAPTERS_OUTBOUND_FILE_PROGRAM_REPOSITORY_H
#define ADAPTERS_OUTBOUND_FILE_PROGRAM_REPOSITORY_H

#include "application/coordinators/control_context.h"
#include "domain/model/wash_program.h"
#include "shared/result_types.h"

/**
 * @file file_program_repository.h
 * @brief 声明基于文件系统的程序仓储绑定入口。
 */

/**
 * @brief 绑定文件程序仓储到系统上下文。
 * @param config_root 程序配置根目录，不能为空。
 */
operation_result_t file_program_repository_init(const char *config_root);

/**
 * @brief 向运行时仓储注入测试或调试用的内存程序。
 * @param wash_program 运行时程序模型，不能为空。
 * @param revision 运行时程序版本号。
 */
void file_program_repository_set_runtime_program(const wash_program_t *wash_program, int revision);

#endif
