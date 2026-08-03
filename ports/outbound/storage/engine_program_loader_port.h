/**
 * @file    engine_program_loader_port.h
 * @brief   控制引擎方案加载端口接口（屏蔽 JSON/二进制等存储格式细节）
 * @author  huwangwei
 * @date    2026-06-28
 *
 * @note    application 层（engine_session / 项目编排器）依赖本接口加载 engine_program_t，
 *          不直接感知 JSON 格式。具体实现：
 *            adapters/outbound/storage/json/engine_program_json.c
 *            （调用 engine_program_json_register_loader）
 *          由项目 wiring 完成注册。
 */

#ifndef PORTS_STORAGE_ENGINE_PROGRAM_LOADER_PORT_H
#define PORTS_STORAGE_ENGINE_PROGRAM_LOADER_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/program_engine/model/engine_model.h"

/* -------------------------------------------------------------------------
 * 方案加载操作表
 * ------------------------------------------------------------------------- */
typedef struct {
    /**
     * @brief  从文件路径加载方案
     * @param  path   文件路径
     * @param  err    错误描述输出缓冲（可为空）
     * @param  errsz  缓冲大小
     * @return 成功返回方案指针（调用方负责 engine_program_free）；失败返回 NULL
     */
    engine_program_t *(*load)(const char *path, char *err, unsigned errsz);

    /**
     * @brief  校验方案资产完整性（可选）
     * @param  path   方案文件路径
     * @param  err    错误描述输出缓冲（可为空）
     * @param  errsz  缓冲大小
     * @retval SW_OK          校验通过
     * @retval SW_ERR_CRC     摘要或长度不匹配
     * @retval 其他            无法读取校验资产
     *
     * @note   为 NULL 表示该实现不提供完整性校验；此时调用方按"跳过校验"处理，
     *         而不是当作失败。校验资产（摘要文件）的命名与格式属于存储格式
     *         细节，由实现自行决定，端口不暴露。
     */
    sw_err_t (*verify_integrity)(const char *path, char *err, unsigned errsz);
} engine_program_loader_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取（由 bootstrap/wiring 调用一次）
 * ------------------------------------------------------------------------- */
sw_err_t                           engine_program_loader_register(const engine_program_loader_ops_t *ops);
const engine_program_loader_ops_t *engine_program_loader_get_ops(void);

/**
 * @brief  加载方案（透传到已注册的实现；未注册则返回 NULL 并填充错误描述）
 */
engine_program_t *engine_program_load(const char *path, char *err, unsigned errsz);

/**
 * @brief  校验方案完整性（透传到已注册实现）
 * @param  path   方案文件路径
 * @param  err    错误描述输出缓冲（可为空）
 * @param  errsz  缓冲大小
 * @retval SW_OK           校验通过，或实现未提供校验能力（视为跳过）
 * @retval SW_ERR_NOT_INIT loader 未注册
 * @retval 其他             实现返回的校验失败原因
 */
sw_err_t engine_program_verify_integrity(const char *path, char *err, unsigned errsz);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_STORAGE_ENGINE_PROGRAM_LOADER_PORT_H */
