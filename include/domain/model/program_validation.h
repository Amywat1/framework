#ifndef DOMAIN_MODEL_PROGRAM_VALIDATION_H
#define DOMAIN_MODEL_PROGRAM_VALIDATION_H

#include "domain/model/wash_program.h"
#include "shared/result_types.h"

/**
 * @file program_validation.h
 * @brief 定义工步程序校验入口。
 */
/**
 * @brief 校验洗车程序整体配置是否合法。
 * @param wash_program 待校验程序对象。
 * @return 成功返回 `operation_result_ok()`，程序不合法时返回失败结果。
 */
operation_result_t program_validation_validate(const wash_program_t *wash_program);

#endif
