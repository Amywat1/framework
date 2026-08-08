/**
 * @file    port_contract.h
 * @brief   项目接入契约：必需端口的启动期集中校验
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    解决的问题：端口未注册与"该项目不需要该端口"在框架看来无法区分，
 *          于是缺失注册能一路启动成功，直到第一次业务调用才失败——现场表现
 *          为随机时刻的功能不可用，而非启动失败，排查成本很高。
 *
 *          做法：项目在 project_validate() 阶段声明自己依赖哪些端口，框架
 *          一次性检查全部声明项并汇总输出缺失清单，任一缺失即启动失败。
 *          框架不内置"哪些端口必需"的判断——那取决于项目用到哪些能力，
 *          例如无云连接的项目本就不该被要求注册 cloud_link。
 */

#ifndef RUNTIME_PORTS_PORT_CONTRACT_H
#define RUNTIME_PORTS_PORT_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdint.h>

/**
 * @brief 可声明为必需的端口位标志
 *
 * 位宽 32，当前用 13 位；新增端口在末尾追加，不改动既有位值
 * （项目侧可能以常量表达式组合这些标志）。
 */
typedef enum {
    PORT_REQ_HAL_IO         = (1U << 0),  /**< 数字 IO */
    PORT_REQ_HAL_SENSOR     = (1U << 1),  /**< 模拟量/传感器 */
    PORT_REQ_HAL_VFD        = (1U << 2),  /**< 变频器 */
    PORT_REQ_HAL_VOICE      = (1U << 3),  /**< 语音播报 */
    PORT_REQ_DEVICE_COMMAND = (1U << 4),  /**< 设备命令入站（由 command_gateway 注册）*/
    PORT_REQ_ALARM_BINDING  = (1U << 5),  /**< 报警触发绑定（由 alarm_registry 注册）*/
    PORT_REQ_MACHINE_OPS    = (1U << 6),  /**< 机型运行时操作 */
    PORT_REQ_CLOUD_LINK     = (1U << 7),  /**< 云连接 */
    PORT_REQ_CLOUD_REPORT   = (1U << 8),  /**< 云上报 */
    PORT_REQ_CLOUD_PROPERTY = (1U << 9),  /**< 云属性下行 */
    PORT_REQ_DEPLOY_STORE   = (1U << 10), /**< 部署配置存储 */
    PORT_REQ_PARAM_STORE    = (1U << 11), /**< 运行参数存储 */
    PORT_REQ_PROGRAM_LOADER = (1U << 12), /**< 洗车方案加载 */
    PORT_REQ_SAFETY         = (1U << 13), /**< 安全输出与急停（cutout / estop / deferred stop）*/
} port_requirement_t;

/**
 * @brief  校验声明的必需端口是否均已注册
 * @param  required  PORT_REQ_* 按位或；传 0 表示无必需端口，直接返回 SW_OK
 * @retval SW_OK           全部已注册
 * @retval SW_ERR_NOT_INIT 存在未注册端口，缺失项已逐条记入 ERROR 日志
 *
 * @note   建议在 project_validate() 中调用，使缺失注册在启动期即失败。
 *         本函数只检查"是否注册"，不校验 ops 内部字段完备性——那由各
 *         *_register() 在注册时按端口自身的必填约定拒绝。
 */
sw_err_t port_contract_validate(uint32_t required);

/**
 * @brief  返回端口位标志对应的可读名称
 * @param  requirement  单个 PORT_REQ_* 值（非组合）
 * @return 名称字符串；未知值返回 "unknown"
 */
const char *port_contract_name(port_requirement_t requirement);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PORTS_PORT_CONTRACT_H */
