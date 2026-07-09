/**
 * @file    alarm_types.h
 * @brief   报警码、等级、响应策略、清除方式与定义类型
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_MODEL_ALARM_TYPES_H
#define DOMAIN_MODEL_ALARM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 报警码编码宏：6 位十进制 = 大类(1) + 编号(3) + 性质(2)
 * ------------------------------------------------------------------------- */
#define ALARM_CODE_MAKE(major, index, nature) \
    ((uint32_t)((major) * 100000U + (index) * 100U + (nature)))

#define ALM_C_POWER   1U
#define ALM_C_SENSE   2U
#define ALM_C_ACT     3U
#define ALM_C_CTRL    4U
#define ALM_C_SW      9U

#define ALM_N_OTHER      0U
#define ALM_N_OVERLOAD   1U
#define ALM_N_COMM_LOST  2U
#define ALM_N_HW_FAULT   3U
#define ALM_N_SIG_ERR    4U
#define ALM_N_TIMEOUT    5U
#define ALM_N_PRESSURE   6U
#define ALM_N_LEVEL      7U
#define ALM_N_SAFETY     9U

#define ALARM_DESC_MAX      48U
#define ALARM_CATALOG_MAX   64U
#define ALARM_ACTIVE_MAX    32U
#define ALARM_SESSION_JOURNAL_MAX  16U
#define ALARM_RECENT_JOURNAL_MAX   64U
#define ALARM_PENDING_EVENT_MAX    32U

#define ALM_SW_ACTIVE_POOL_OVERFLOW  2U
#define ALARM_CODE_ACTIVE_POOL_OVERFLOW \
    ALARM_CODE_MAKE(ALM_C_SW, ALM_SW_ACTIVE_POOL_OVERFLOW, ALM_N_OTHER)

typedef enum
{
    ALARM_LEVEL_MINOR = 0,
    ALARM_LEVEL_MAJOR,
    ALARM_LEVEL_CRITICAL,
} alarm_level_t;

typedef enum
{
    RESP_LOG_ONLY = 0,
    RESP_COMPLETE_THEN_ASSESS,
    RESP_STOP_IMMEDIATELY,
} response_strategy_t;

typedef enum
{
    ALARM_CLEAR_AUTO_STATIC = 0,
    ALARM_CLEAR_ON_MOTION,
    ALARM_CLEAR_MANUAL_RESET,
} alarm_clear_t;

typedef enum
{
    ALARM_SOURCE_LEVEL = 0,
    ALARM_SOURCE_COMM,
    ALARM_SOURCE_PROCESS,
    ALARM_SOURCE_CALLSITE,
} alarm_source_kind_t;

typedef enum
{
    ALARM_SCOPE_NONE = 0,
    ALARM_SCOPE_GANTRY,
    ALARM_SCOPE_TOP_BRUSH,
    ALARM_SCOPE_SIDE_BRUSH,
    ALARM_SCOPE_FRONT_WHEEL,
    ALARM_SCOPE_REAR_WHEEL_LOCK,
    ALARM_SCOPE_FAN,
    ALARM_SCOPE_WATER,
    ALARM_SCOPE_COUNT
} alarm_scope_t;

typedef enum
{
    ALARM_CODE_NONE = 0U,
} alarm_code_t;

typedef struct
{
    uint32_t               code;
    alarm_level_t          level;
    response_strategy_t    response;
    alarm_clear_t          clear;
    alarm_source_kind_t    source_kind;
    alarm_scope_t          scope;
    bool                   immediate_cutout;
    char                   desc[ALARM_DESC_MAX];
} alarm_def_t;

typedef enum
{
    ALARM_INSTANCE_ACTIVE = 0,
    ALARM_INSTANCE_CLEARED,
} alarm_instance_state_t;

typedef struct
{
    uint32_t               code;
    alarm_level_t          level;
    response_strategy_t    response;
    alarm_clear_t          clear;
    alarm_instance_state_t state;
    uint64_t               triggered_at_ms;
} alarm_instance_t;

typedef enum
{
    ALARM_DOMAIN_EVT_TRIGGERED = 0,
    ALARM_DOMAIN_EVT_CLEARED,
    ALARM_DOMAIN_EVT_BATCH_CLEARED,
} alarm_domain_event_kind_t;

typedef struct
{
    alarm_domain_event_kind_t kind;
    uint32_t                  code;
} alarm_domain_event_t;

/**
 * @brief  按等级推导缺省响应策略
 */
static inline response_strategy_t alarm_default_response(alarm_level_t level)
{
    switch (level)
    {
    case ALARM_LEVEL_CRITICAL:
        return RESP_STOP_IMMEDIATELY;
    case ALARM_LEVEL_MAJOR:
        return RESP_COMPLETE_THEN_ASSESS;
    default:
        return RESP_LOG_ONLY;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MODEL_ALARM_TYPES_H */
