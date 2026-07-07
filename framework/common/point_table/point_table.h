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

/** 点位数据类型 */
typedef enum
{
    POINT_TYPE_BOOL = 0,
    POINT_TYPE_INT,
    POINT_TYPE_STRING,
} point_type_t;

/** 点位值（按 point_type_t 使用对应成员） */
typedef struct
{
    bool    b;
    int32_t i;
    char    s[POINT_STR_MAX];
} point_value_t;

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
 * @brief  遍历点表，将所有 get!=NULL 的点位序列化为 JSON
 * @param  entries   点位表
 * @param  count     点位数量
 * @param  buf       输出缓冲区
 * @param  buf_size  缓冲区大小
 * @retval SW_OK       序列化成功
 * @retval SW_ERR_PARAM 缓冲区不足或序列化失败
 */
sw_err_t point_table_to_json(const point_table_entry_t *entries, size_t count,
                              char *buf, size_t buf_size);

/**
 * @brief  解析 {"标识符":值, ...} 形式 JSON，逐 key 查表调用对应 set()
 * @param  entries   点位表
 * @param  count     点位数量
 * @param  json_str  JSON 字符串
 * @note   未知标识符、类型不匹配或只读点位写入仅记录告警日志，
 *         不影响同一消息内其它属性的处理
 */
void point_table_from_json(const point_table_entry_t *entries, size_t count,
                            const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_COMMON_POINT_TABLE_H */
