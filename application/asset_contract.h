/**
 * @file    asset_contract.h
 * @brief   项目接入契约：必需资产的启动期集中校验
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    与 port_contract 的分工：
 *          port_contract 检查"函数表是否注册"，本文件检查"数据资产是否就位"。
 *          两者是同一类问题的两面——端口注册了但资产没加载，业务链路同样会在
 *          第一次真实调用时才失败，而启动日志一切正常。
 *
 *          典型现场：报警目录未加载时，alarm_registry 把任何报警码都解析不到
 *          定义，trigger 静默失败——设备"从不报警"，而不是"启动失败"。这类
 *          问题只能靠触发一次真实故障才发现，排查成本远高于启动期拦住。
 *
 *          框架不内置"哪些资产必需"的判断：不用方案引擎的项目本就没有 IO
 *          目录，不接云的项目也不该被要求提供物模型。故由项目声明。
 */

#ifndef APPLICATION_ASSET_CONTRACT_H
#define APPLICATION_ASSET_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdint.h>

/**
 * @brief 可声明为必需的资产位标志
 *
 * 位宽 32，当前用 3 位；新增资产在末尾追加，不改动既有位值
 * （项目侧可能以常量表达式组合这些标志）。
 */
typedef enum {
    ASSET_REQ_ALARM_CATALOG     = (1U << 0), /**< 报警定义目录（经 alarm_binding.load_catalog 加载）*/
    ASSET_REQ_CLOUD_POINT_TABLE = (1U << 1), /**< 云端物模型点位表（经 cloud_model_register 注册）*/
    ASSET_REQ_ENGINE_IO_CATALOG = (1U << 2), /**< 方案引擎 IO 目录（经 engine_io_register 注册）*/
} asset_requirement_t;

/**
 * @brief  校验声明的必需资产是否均已就位
 * @param  required  ASSET_REQ_* 按位或；传 0 表示无必需资产，直接返回 SW_OK
 * @retval SW_OK           全部就位
 * @retval SW_ERR_NOT_INIT 存在缺失资产，缺失项已逐条记入 ERROR 日志
 *
 * @note   与 port_contract_validate 一样逐项检查并全部报出，而不是遇到首个
 *         缺失就返回——接入调试时一次看到完整缺失清单更省时间。
 * @note   调用时机：project_validate() 中、port_contract_validate() 之后。
 *         须在 bind 阶段之后，因为报警目录由 project_bind_alarm_catalog 加载。
 * @note   本函数只检查资产"是否存在且非空"，不校验内容正确性。点位表的字段
 *         级校验由 cloud_point_validate 负责，两者互补：前者答"有没有"，
 *         后者答"对不对"。
 */
sw_err_t asset_contract_validate(uint32_t required);

/**
 * @brief  返回资产位标志对应的可读名称
 * @param  requirement  单个 ASSET_REQ_* 值（非组合）
 * @return 名称字符串；未知值返回 "unknown"
 */
const char *asset_contract_name(asset_requirement_t requirement);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ASSET_CONTRACT_H */
