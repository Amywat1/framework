#ifndef DOMAIN_MODEL_PROGRAM_VALIDATION_H
#define DOMAIN_MODEL_PROGRAM_VALIDATION_H

#include "domain/model/wash_program.h"
#include "shared/result_types.h"

/**
 * @file program_validation.h
 * @brief 定义工步程序校验入口。
 */
operation_result_t program_validation_validate(const wash_program_t *wash_program);

#endif
