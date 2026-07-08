/**
 * @file    engine_program_validate.h
 * @brief   洗车方案语义校验（加载期引用完整性、IO 名、表达式变量）
 * @author  huwangwei
 * @date    2026-07-08
 *
 * @note    在 JSON 结构解析成功后调用；catalog 为空时跳过 IO 枚举名校验。
 */

#ifndef DOMAIN_ENGINE_ENGINE_PROGRAM_VALIDATE_H
#define DOMAIN_ENGINE_ENGINE_PROGRAM_VALIDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include "framework/domain/wash/model/engine_model.h"
#include "framework/domain/wash/engine/engine_io.h"

/**
 * @brief  校验方案语义
 * @param  prog     已解析的方案，不可为空
 * @param  catalog  IO 名称目录，可为空
 * @param  err      错误描述输出缓冲，可为空
 * @param  errsz    缓冲大小
 * @retval SW_OK           校验通过
 * @retval SW_ERR_PARAM    校验失败
 */
sw_err_t engine_program_validate(const engine_program_t *prog,
                                 const engine_io_catalog_t *catalog,
                                 char *err, unsigned errsz);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_ENGINE_ENGINE_PROGRAM_VALIDATE_H */
