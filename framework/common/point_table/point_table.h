/**
 * @file    point_table.h
 * @brief   标识符点位表通用引擎接口
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    传输无关：定义"标识符+类型+get/set"点位表，以及 JSON 序列化/反序列化。
 *          具体点位由调用方登记；云端物模型、CLI 调试等场景均可复用。
 */

#ifndef FRAMEWORK_COMMON_POINT_TABLE_H
#define FRAMEWORK_COMMON_POINT_TABLE_H

#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 字符串型点位的最大长度（含终止符） */
#define POINT_STR_MAX   32U

/** 点位 id 最大长度（用于结果记录） */
#define POINT_ID_MAX    32U

/** 点位数据类型 */
typedef enum
{
    POINT_TYPE_BOOL = 0,
    POINT_TYPE_INT,
    POINT_TYPE_STRING,
    POINT_TYPE_FLOAT,
} point_type_t;

/** 点位值（按 point_type_t 使用对应成员） */
typedef struct
{
    bool    b;
    int32_t i;
    float   f;
    char    s[POINT_STR_MAX];
} point_value_t;

/** 上行 get 失败时的处理策略 */
typedef enum
{
    POINT_GET_FAIL_OMIT = 0,   /**< 跳过该 key（默认） */
    POINT_GET_FAIL_ABORT,      /**< 整包 build 失败 */
    POINT_GET_FAIL_NULL,       /**< 写入 JSON null */
} point_get_fail_policy_t;

/**
 * @brief  JSON 应用/序列化结果汇总
 */
typedef struct
{
    size_t   total_keys;
    size_t   applied;
    size_t   rejected;
    size_t   skipped_get;
    sw_err_t first_error;
    char     first_error_id[POINT_ID_MAX];
} point_apply_result_t;

/**
 * @brief  读点位：读取当前值填入 out
 * @retval SW_OK 取值成功；其它值时该点位本次序列化被跳过
 */
typedef sw_err_t (*point_get_fn_t)(point_value_t *out);

/**
 * @brief  写点位：执行 in 携带的写操作
 * @retval SW_OK 执行成功
 */
typedef sw_err_t (*point_set_fn_t)(const point_value_t *in);

/** 点位表条目（只读点位 set=NULL；纯命令点位 get 可返回恒定值供回显）*/
typedef struct
{
    const char     *id;
    point_type_t    type;
    point_get_fn_t  get;
    point_set_fn_t  set;
} point_table_entry_t;

/**
 * @brief  初始化结果结构体
 */
void point_apply_result_init(point_apply_result_t *result);

/**
 * @brief  遍历点表，将所有 get!=NULL 的点位序列化为 JSON
 */
sw_err_t point_table_to_json(const point_table_entry_t *entries, size_t count,
                              char *buf, size_t buf_size);

/**
 * @brief  遍历点表序列化，并可选统计 get 失败数
 */
sw_err_t point_table_to_json_ex(const point_table_entry_t *entries, size_t count,
                                 char *buf, size_t buf_size,
                                 point_get_fail_policy_t fail_policy,
                                 point_apply_result_t *result_opt);

/**
 * @brief  将指定 id 列表对应的可读点位序列化为 JSON
 */
sw_err_t point_table_to_json_filtered(const point_table_entry_t *entries, size_t count,
                                     const char *const *ids, size_t id_count,
                                     char *buf, size_t buf_size);

/**
 * @brief  解析 JSON 并逐 key 调用 set()，汇总处理结果
 */
sw_err_t point_table_apply_json(const point_table_entry_t *entries, size_t count,
                                 const char *json_str,
                                 point_apply_result_t *result_opt);

/**
 * @brief  解析 JSON 并逐 key 调用 set()（兼容入口，不返回结果）
 */
void point_table_from_json(const point_table_entry_t *entries, size_t count,
                            const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_COMMON_POINT_TABLE_H */
