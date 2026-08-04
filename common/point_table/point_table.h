/**
 * @file    point_table.h
 * @brief   标识符点位表通用引擎接口
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    传输无关且序列化无关：只定义"标识符+类型+get/set"点位表模型与查表。
 *          具体点位由调用方登记；云端物模型、CLI 调试等场景均可复用。
 *
 * @note    JSON 编解码不在此处，见
 *          `adapters/outbound/serialization/json/point_table_json.h`。
 *          分开的理由：本文件是 `common/` 的一部分，而 `common/` 应对上层与外部
 *          格式均无依赖。编解码留在这里会让最底层绑定一种序列化格式，使
 *          「domain 不解析序列化格式」（边界规则 R9b）失去基础——domain 依赖
 *          common，而 common 自己就 include cJSON。
 */

#ifndef COMMON_POINT_TABLE_POINT_TABLE_H
#define COMMON_POINT_TABLE_POINT_TABLE_H

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 字符串型点位的最大长度（含终止符） */
#define POINT_STR_MAX 32U

/** 点位 id 最大长度（用于结果记录） */
#define POINT_ID_MAX 32U

/** 点位数据类型 */
typedef enum {
    POINT_TYPE_BOOL = 0,
    POINT_TYPE_INT,
    POINT_TYPE_STRING,
    POINT_TYPE_FLOAT,
} point_type_t;

/** 点位值（按 point_type_t 使用对应成员） */
typedef struct {
    bool    b;
    int32_t i;
    float   f;
    char    s[POINT_STR_MAX];
} point_value_t;

/** 上行 get 失败时的处理策略 */
typedef enum {
    POINT_GET_FAIL_OMIT = 0, /**< 跳过该 key（默认） */
    POINT_GET_FAIL_ABORT,    /**< 整包 build 失败 */
    POINT_GET_FAIL_NULL,     /**< 写入 JSON null */
} point_get_fail_policy_t;

/**
 * @brief  JSON 应用/序列化结果汇总
 */
typedef struct {
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

/** 点位表条目（只读点位 set=NULL；脉冲命令 get 用 echo_idle；持续命令 get 读运行态）*/
typedef struct {
    const char    *id;
    point_type_t   type;
    point_get_fn_t get;
    point_set_fn_t set;
} point_table_entry_t;

/**
 * @brief  初始化结果结构体
 */
void point_apply_result_init(point_apply_result_t *result);

/**
 * @brief  记录首个错误（仅当尚未记录时写入）
 * @param  result 结果结构体，可为 NULL
 * @param  id     出错点位 id，可为 NULL
 * @param  err    错误码
 */
void point_apply_result_record_error(point_apply_result_t *result, const char *id, sw_err_t err);

/**
 * @brief  按 id 查找点位表条目
 * @param  entries 点位表数组
 * @param  count   条目数量
 * @param  id      点位标识符
 * @return 匹配条目指针，未找到返回 NULL
 */
const point_table_entry_t *point_table_find_entry(const point_table_entry_t *entries, size_t count, const char *id);

/**
 * @brief  按 id 在等步长条目数组中查找（首成员须为 point_table_entry_t）
 * @param  entries      条目数组首地址
 * @param  count        条目数量
 * @param  entry_stride 单条记录字节跨度
 * @param  id           点位标识符
 * @return 匹配条目的 base 指针，未找到返回 NULL
 */
const point_table_entry_t *point_table_find_entry_at(const void *entries,
                                                     size_t      count,
                                                     size_t      entry_stride,
                                                     const char *id);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_POINT_TABLE_POINT_TABLE_H */
