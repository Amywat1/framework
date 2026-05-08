#ifndef ADAPTERS_CONFIG_JSON_PROGRAM_PARSER_H
#define ADAPTERS_CONFIG_JSON_PROGRAM_PARSER_H

#include "domain/model/wash_program.h"
#include "shared/result_types.h"

/**
 * @file json_program_parser.h
 * @brief 声明洗车工步程序 JSON 配置到强类型领域模型的解析入口。
 */

/**
 * @brief 解析程序配置文件并输出强类型洗车程序模型。
 * @param config_path JSON 配置文件路径，不能为空。
 * @param wash_program 解析成功后的程序模型输出，不能为空。
 * @return 解析成功返回成功结果；字段非法、语义冲突或文件读取失败时返回失败结果。
 */
operation_result_t json_program_parser_parse(const char *config_path, wash_program_t *wash_program);

#endif
