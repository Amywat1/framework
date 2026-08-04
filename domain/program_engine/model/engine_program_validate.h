/**
 * @file    engine_program_validate.h
 * @brief   控制方案语义校验（加载期引用完整性、信号/轴/资源名、表达式变量）
 * @author  huwangwei
 * @date    2026-07-08
 *
 * @note    在 JSON 结构解析成功后调用；目录为空时跳过对应名校验。
 */

#ifndef DOMAIN_PROGRAM_ENGINE_MODEL_ENGINE_PROGRAM_VALIDATE_H
#define DOMAIN_PROGRAM_ENGINE_MODEL_ENGINE_PROGRAM_VALIDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/program_engine/engine/engine_actuator.h"
#include "domain/program_engine/engine/engine_io.h"
#include "domain/program_engine/model/engine_model.h"

/**
 * @brief  校验方案语义
 * @param  prog         已解析的方案，不可为空
 * @param  io_catalog   DI/轴目录，可为空
 * @param  act_catalog  执行机构资源/水路路径目录，可为空
 * @param  err          错误描述输出缓冲，可为空
 * @param  errsz        缓冲大小
 * @retval SW_OK        校验通过
 * @retval SW_ERR_PARAM 校验失败
 */
sw_err_t engine_program_validate(const engine_program_t          *prog,
                                 const engine_io_catalog_t       *io_catalog,
                                 const engine_actuator_catalog_t *act_catalog,
                                 char                            *err,
                                 unsigned                         errsz);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROGRAM_ENGINE_MODEL_ENGINE_PROGRAM_VALIDATE_H */
