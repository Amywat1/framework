/**
 * @file    port_registry.h
 * @brief   端口注册表统一生命周期语义
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    本头文件只声明跨注册表通用的约定与测试复位入口，
 *          各端口的 register/get_ops 声明仍留在各自端口头文件中。
 */

#ifndef RUNTIME_PORTS_PORT_REGISTRY_H
#define RUNTIME_PORTS_PORT_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @par 注册语义（所有 *_register 一致遵守）：
 *
 * - `ops == NULL`：解除注册，后续 `*_get_ops()` 返回 NULL，返回 SW_OK。
 *   这是受支持的显式操作，测试与运行期降级均可使用。
 * - `ops != NULL` 且缺少必填函数指针：拒绝，返回 SW_ERR_PARAM，
 *   保持原有注册不变（不会被半个 ops 覆盖）。
 *   必填字段仅限调用方会无条件解引用的入口，详见各端口头文件说明。
 * - 重复注册合法函数表：以最后一次为准（替换），返回 SW_OK。
 *   框架不禁止运行期替换 provider，但装配阶段之后替换属于项目自身职责。
 *
 * @par 复位：
 *   按注册表分层提供，避免测试为了一个复位函数被迫链接全部三个注册表。
 *   仅供单元测试消除用例间的全局状态残留，生产路径不应调用。
 */

/** @brief 清空 HAL 层端口注册（IO / 传感器 / 变频器 / 语音）*/
void port_registry_hal_reset(void);

/** @brief 清空基础设施层端口注册（设备命令 / 报警绑定 / 机型运行时操作）*/
void port_registry_infra_reset(void);

/** @brief 清空云与存储层端口注册（link / deploy / param）*/
void port_registry_cloud_reset(void);

/** @brief 清空安全端口注册（cutout / estop / alarm 判定 / deferred stop）*/
void port_registry_safety_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PORTS_PORT_REGISTRY_H */
