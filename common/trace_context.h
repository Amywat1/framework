/**
 * @file    trace_context.h
 * @brief   跨线程业务因果上下文
 */

#ifndef COMMON_TRACE_CONTEXT_H
#define COMMON_TRACE_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief 一条业务链路的稳定标识集合。 */
typedef struct {
    uint64_t wash_session_id; /**< 洗车会话编号，非洗车流程为 0 */
    uint64_t command_id;      /**< 当前根命令编号，无命令来源为 0 */
    uint64_t correlation_id;  /**< 整条关联链编号 */
    uint64_t causation_id;    /**< 直接父命令或父事件编号 */
} trace_context_t;

/**
 * @brief 获取当前线程的因果上下文。
 * @return 当前上下文的值拷贝；未设置时各字段为 0。
 */
trace_context_t trace_context_get(void);

/**
 * @brief 设置当前线程的因果上下文。
 * @param context 上下文；传 NULL 表示清空。
 */
void trace_context_set(const trace_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_TRACE_CONTEXT_H */
