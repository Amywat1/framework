/**
 * @file    engine_actuator.h
 * @brief   通用控制引擎执行机构意图端口（机型无关）
 * @author  huwangwei
 * @date    2026-07-17
 *
 * @note    引擎只提交不透明意图（resource/cmd/dir/gear/paths），不写 DO。
 *          项目注册 ops 与 catalog，将意图映射到本机机构 API。
 */

#ifndef DOMAIN_PROGRAM_ENGINE_ENGINE_ENGINE_ACTUATOR_H
#define DOMAIN_PROGRAM_ENGINE_ENGINE_ENGINE_ACTUATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/program_engine/model/engine_model.h"

/**
 * @brief  项目声明的资源/水路路径名目录（加载期校验）
 */
typedef struct {
    const char *const *resources;   /**< 资源名，如 "gantry" */
    unsigned           resource_count;
    const char *const *water_paths; /**< 水路路径名，可为空 */
    unsigned           water_path_count;
} engine_actuator_catalog_t;

/**
 * @brief  执行机构操作集
 */
typedef struct {
    /**
     * @brief  应用意图
     * @param  intent  意图，不可为空
     * @retval SW_OK 成功；其它为错误
     */
    sw_err_t (*apply)(const engine_intent_t *intent);

    /**
     * @brief  释放资源（阶段退出/control 失活时调用）
     * @param  resource  资源名，不可为空
     * @retval SW_OK 成功；其它为错误
     */
    sw_err_t (*release)(const char *resource);

    /**
     * @brief  全停（联锁 halt_all / 紧急停机语义，与项目手动全停对齐）
     * @retval SW_OK 成功；其它为错误
     */
    sw_err_t (*halt_all)(void);
} engine_actuator_ops_t;

typedef struct {
    const engine_actuator_ops_t     *ops;
    const engine_actuator_catalog_t *catalog;
} engine_actuator_backend_t;

/**
 * @brief  注册执行机构后端（引擎初始化前调用）
 * @param  backend  后端；ops 字段不全则忽略；catalog 可为空表示跳过名校验
 */
void engine_actuator_register(const engine_actuator_backend_t *backend);

/**
 * @brief  获取已注册操作集
 * @return 操作集；未注册返回 NULL
 */
const engine_actuator_ops_t *engine_actuator_get_ops(void);

/**
 * @brief  获取已注册目录
 * @return 目录；未注册返回 NULL
 */
const engine_actuator_catalog_t *engine_actuator_get_catalog(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROGRAM_ENGINE_ENGINE_ENGINE_ACTUATOR_H */
