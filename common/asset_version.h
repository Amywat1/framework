/**
 * @file    asset_version.h
 * @brief   项目资产 schema 版本校验（通用 helper）
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    解决的问题：项目资产（方案、云物模型、报警目录、HAL binding、水路
 *          拓扑、部署配置）与框架版本不匹配时，当前多数资产不会在启动期失败，
 *          而是在运行时表现为字段错位或行为异常——现场很难把这类症状追溯到
 *          "资产版本不对"。
 *
 *          做法：框架只提供版本表达与兼容判定，不规定资产格式。资产本身仍留在
 *          项目侧，框架不感知其内容。
 *
 * @note    兼容规则采用语义化版本的常规约定：
 *            主版本不同   → 不兼容（结构性变更）
 *            次版本更高   → 资产比框架新，不兼容（可能含框架不认识的字段）
 *            次版本更低   → 兼容（框架向后兼容旧资产）
 *          修订号不参与兼容判定，仅用于诊断输出。
 *
 *          这里刻意不做"未知字段自动忽略"这类宽松处理：对安全相关资产
 *          （报警目录、水路拓扑），静默忽略未识别字段等于让缺失的约束通过校验。
 */

#ifndef COMMON_ASSET_VERSION_H
#define COMMON_ASSET_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 语义化版本号 */
typedef struct {
    uint16_t major; /**< 主版本：结构性变更 */
    uint16_t minor; /**< 次版本：向后兼容的追加 */
    uint16_t patch; /**< 修订号：不参与兼容判定 */
    bool     valid; /**< 解析是否成功 */
} asset_version_t;

/**
 * @brief  解析 "主.次[.修订]" 形式的版本串
 * @param  text  版本串；可为 NULL（返回 valid=false）
 * @return 解析结果；失败时 valid 为 false 且各字段为 0
 *
 * @note   接受 "1.0" 与 "1.0.3" 两种写法；修订号缺省为 0。
 *         不接受前后空白、前导零之外的任何额外字符——资产版本是机器生成的
 *         字段，宽松解析只会掩盖生成端的问题。
 */
asset_version_t asset_version_parse(const char *text);

/**
 * @brief  判断资产版本是否被框架支持的版本接受
 * @param  asset      资产声明的版本
 * @param  supported  框架当前支持的版本
 * @retval true  兼容，可加载
 *
 * @note   判定规则见文件头注释。asset 解析失败时一律返回 false。
 */
bool asset_version_is_compatible(asset_version_t asset, asset_version_t supported);

/**
 * @brief  一步完成解析与兼容校验，失败时填充可读原因
 * @param  asset_name      资产名称，用于错误描述（例如 "cloud_model"）
 * @param  asset_text      资产声明的版本串
 * @param  supported_text  框架支持的版本串
 * @param  err             错误描述输出缓冲，可为 NULL
 * @param  errsz           缓冲大小
 * @retval SW_OK        兼容
 * @retval SW_ERR_PARAM 版本串无法解析
 * @retval SW_ERR_STATE 可解析但不兼容
 *
 * @note   区分 PARAM 与 STATE 便于诊断：前者是资产格式坏了（生成端问题），
 *         后者是版本不匹配（部署问题），两者的排查方向完全不同。
 */
sw_err_t asset_version_check(const char *asset_name,
                             const char *asset_text,
                             const char *supported_text,
                             char       *err,
                             unsigned    errsz);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_ASSET_VERSION_H */
