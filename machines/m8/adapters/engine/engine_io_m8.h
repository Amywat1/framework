/**
 * @file    engine_io_m8.h
 * @brief   M8 机型引擎 IO 后端（将通道名映射到设备驱动 API）
 * @author  huwangwei
 * @date    2026-06-27
 *
 * @note    在 wash_orchestrator_init() 调用前由应用主入口（app_main / wiring）
 *          调用一次 engine_io_m8_register()，此后引擎通过 engine_io 接口
 *          经本模块读写所有硬件 IO。
 */

#ifndef MACHINES_M8_ADAPTERS_ENGINE_ENGINE_IO_M8_H
#define MACHINES_M8_ADAPTERS_ENGINE_ENGINE_IO_M8_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  将 M8 机型引擎 IO 后端注册到引擎 IO 接口（engine_io_register）
 * @note   仅可调用一次；重复调用以最后一次为准。
 */
void engine_io_m8_register(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_ENGINE_ENGINE_IO_M8_H */
