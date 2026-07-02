/**
 * @file    tsl_point.h
 * @brief   物模型点位表通用引擎接口（Thing Spec List）
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    机型无关：只定义"标识符+类型+get/set"点位表的数据形状，以及
 *          通用的序列化（点表→JSON）/分发（JSON→点表）逻辑。任何机型的
 *          具体点位数据与 get/set 实现放在各自的 machines/<machine>/ 下，
 *          通过本模块的函数完成上行/下行，不需要各机型重复实现遍历逻辑。
 */

#ifndef ADAPTERS_CLOUD_TSL_POINT_H
#define ADAPTERS_CLOUD_TSL_POINT_H

#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 物模型字符串型点位的最大长度（含终止符） */
#define TSL_STR_MAX   32U

/** 点位数据类型 */
typedef enum
{
    TSL_BOOL = 0,
    TSL_INT,
    TSL_STRING,
} tsl_type_t;

/** 点位值（按 tsl_type_t 使用对应成员） */
typedef struct
{
    bool    b;
    int32_t i;
    char    s[TSL_STR_MAX];
} tsl_value_t;

/**
 * @brief  上行取值：读取点位当前值填入 out
 * @retval SW_OK 取值成功；其它值时该点位本次上报被跳过
 */
typedef sw_err_t (*tsl_get_fn_t)(tsl_value_t *out);

/**
 * @brief  下行赋值：执行 in 携带的写操作
 * @retval SW_OK 执行成功
 */
typedef sw_err_t (*tsl_set_fn_t)(const tsl_value_t *in);

/** 物模型点位定义（只读点位 set=NULL；纯命令点位 get 可返回恒定值供回显）*/
typedef struct
{
    const char   *id;
    tsl_type_t    type;
    tsl_get_fn_t  get;
    tsl_set_fn_t  set;
} tsl_point_t;

/**
 * @brief  遍历点表，将所有 get!=NULL 的点位序列化为 JSON
 * @param  points    点位表
 * @param  count     点位数量
 * @param  buf       输出缓冲区
 * @param  buf_size  缓冲区大小
 * @retval SW_OK       序列化成功
 * @retval SW_ERR_PARAM 缓冲区不足或序列化失败
 */
sw_err_t tsl_build_report_json(const tsl_point_t *points, size_t count,
                                char *buf, size_t buf_size);

/**
 * @brief  解析 {"标识符":值, ...} 形式 JSON，逐 key 查表调用对应点位 set()
 * @param  points    点位表
 * @param  count     点位数量
 * @param  json_str  下行 JSON 字符串
 * @note   未知标识符、类型不匹配或只读点位写入仅记录告警日志，
 *         不影响同一消息内其它属性的处理
 */
void tsl_command_dispatch(const tsl_point_t *points, size_t count,
                           const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_CLOUD_TSL_POINT_H */
