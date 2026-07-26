/**
 * @file    trace_context.c
 * @brief   跨线程业务因果上下文实现
 */

#include "common/trace_context.h"

#include <string.h>

static _Thread_local trace_context_t s_trace_context;

trace_context_t trace_context_get(void)
{
    return s_trace_context;
}

void trace_context_set(const trace_context_t *context)
{
    if (context == NULL) {
        memset(&s_trace_context, 0, sizeof(s_trace_context));
        return;
    }
    s_trace_context = *context;
}
