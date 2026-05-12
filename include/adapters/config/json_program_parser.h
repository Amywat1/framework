#ifndef ADAPTERS_CONFIG_JSON_PROGRAM_PARSER_H
#define ADAPTERS_CONFIG_JSON_PROGRAM_PARSER_H

#include "domain/model/wash_program.h"
#include "shared/result_types.h"

/**
 * @file json_program_parser.h
 * @brief 声明洗车程序 JSON 配置到强类型领域模型的解析入口。
 */

/**
 * @brief 解析程序配置文件并输出强类型洗车程序模型。
 * @param config_path JSON 配置文件路径，不能为空，且必须可读取。
 * @param wash_program 解析成功后的程序模型输出，不能为空。
 * @return 成功时返回成功结果；参数非法返回 `ERROR_CODE_INVALID_ARGUMENT`；
 *         文件读取失败返回 `ERROR_CODE_IO_FAILED`；JSON 语法错误、重复键、
 *         字段缺失、字段类型错误或领域语义非法时返回 `ERROR_CODE_PARSE_FAILED`；
 *         输入旧 `stages` 顶层结构或不受支持的控制对象时返回 `ERROR_CODE_UNSUPPORTED`。
 */
operation_result_t json_program_parser_parse(const char *config_path, wash_program_t *wash_program);

#endif
