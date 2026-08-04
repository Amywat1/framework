/**
 * @file    engine_io.h
 * @brief   通用控制引擎 IO 后端接口（按名读 DI / 坐标轴）
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    引擎通过本接口读取 DI 与坐标轴。写侧由 engine_actuator 提交机构意图。
 */

#ifndef DOMAIN_PROGRAM_ENGINE_ENGINE_ENGINE_IO_H
#define DOMAIN_PROGRAM_ENGINE_ENGINE_ENGINE_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>

/**
 * @brief  引擎 IO 名称目录（供方案加载期校验 signal/axis）
 */
typedef struct {
    const char *const *signals; /**< DI 信号枚举名数组 */
    unsigned           signal_count;
    const char *const *axes;    /**< 坐标轴 ID 数组 */
    unsigned           axis_count;
} engine_io_catalog_t;

/**
 * @brief  引擎 IO 后端操作集（读侧）
 */
typedef struct {
    /**
     * @brief  读取 DI 信号的逻辑值
     * @param  name  DI 枚举名
     * @return 信号逻辑值（未知名按 0）
     */
    int (*read_signal)(const char *name);

    /**
     * @brief  读取坐标轴当前位置/速度/有效性
     * @retval SW_OK        成功
     * @retval SW_ERR_PARAM 参数非法或名称未知
     */
    sw_err_t (*read_axis)(const char *name, double *out_pos, double *out_speed, bool *out_valid);
} engine_io_ops_t;

typedef struct {
    const engine_io_ops_t     *ops;
    const engine_io_catalog_t *catalog;
} engine_io_backend_t;

void                       engine_io_register(const engine_io_backend_t *backend);
const engine_io_ops_t     *engine_io_get_ops(void);
const engine_io_catalog_t *engine_io_get_catalog(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROGRAM_ENGINE_ENGINE_ENGINE_IO_H */
