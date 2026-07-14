/**
 * @file    engine_io.h
 * @brief   通用控制引擎 IO 后端接口（按枚举名读写 DI/DO 与坐标轴）
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    引擎位于 domain 层，不感知任何硬件实现。它通过本接口按"枚举名"
 *          读取 DI 信号、读取坐标轴位置、写入 DO 输出。具体后端（仿真内存表
 *          或真机 HAL 桥接）由 adapters 层实现并在引擎初始化前注册。
 *          对应规格说明 §2：硬件层按名注册枚举名，配置层按名引用。
 */

#ifndef DOMAIN_ENGINE_ENGINE_IO_H
#define DOMAIN_ENGINE_ENGINE_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>

/**
 * @brief  引擎 IO 名称目录（供方案加载期校验 signal/channel/axis 引用）
 */
typedef struct {
    const char *const *signals; /**< DI 信号枚举名数组 */
    unsigned           signal_count;
    const char *const *outputs; /**< DO 通道枚举名数组 */
    unsigned           output_count;
    const char *const *axes;    /**< 坐标轴 ID 数组 */
    unsigned           axis_count;
} engine_io_catalog_t;

/**
 * @brief  引擎 IO 后端操作集
 *
 * 所有函数指针在注册时必须非空。引擎在每个 tick 通过本接口与外界交互。
 */
typedef struct {
    /**
     * @brief  读取 DI 信号的逻辑值
     * @param  name  硬件层注册的 DI 枚举名（如 "ESTOP"、"GANTRY_FWD_LIMIT"）
     * @return 信号逻辑值（整数；布尔信号为 0/1，未知名按 0 处理）
     */
    int (*read_signal)(const char *name);

    /**
     * @brief  读取坐标轴当前位置/速度/有效性
     * @param  name      坐标轴 ID（如 "gantry"）
     * @param  out_pos   输出当前位置（工程单位），不可为空
     * @param  out_speed 输出当前速度，不可为空
     * @param  out_valid 输出位置是否有效，不可为空
     * @retval SW_OK        读取成功
     * @retval SW_ERR_PARAM 名称未知或参数为空
     */
    sw_err_t (*read_axis)(const char *name, double *out_pos, double *out_speed, bool *out_valid);

    /**
     * @brief  写入 DO 输出
     * @param  name   硬件层注册的 DO 枚举名（如 "GANTRY_FWD"）
     * @param  value  输出整数值（布尔输出用 0/1，多档位用具体档位值）
     */
    void (*write_output)(const char *name, int value);
} engine_io_ops_t;

/**
 * @brief  引擎 IO 后端（操作集与名称目录的组合）
 */
typedef struct {
    const engine_io_ops_t     *ops;
    const engine_io_catalog_t *catalog;
} engine_io_backend_t;

/**
 * @brief  注册引擎 IO 后端（引擎初始化前调用一次）
 * @param  backend  后端组合；为空或 ops 字段不全将被忽略；catalog 可为空表示跳过 IO 名校验
 */
void engine_io_register(const engine_io_backend_t *backend);

/**
 * @brief  获取已注册的引擎 IO 后端
 * @return 后端操作集指针；未注册时返回 NULL
 */
const engine_io_ops_t *engine_io_get_ops(void);

/**
 * @brief  获取已注册的 IO 名称目录
 * @return 目录指针；未注册时返回 NULL
 */
const engine_io_catalog_t *engine_io_get_catalog(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_ENGINE_ENGINE_IO_H */
