/**
 * @file    engine_model.h
 * @brief   通用控制引擎方案数据模型（program/phase/lane/step/...）
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    本模型是配置（YAML 为人维护源、运行期由 cJSON 解析其 JSON 产物）
 *          解析后的内存表示，为纯数据 + 已编译表达式。运行时（engine.c）只读本
 *          模型。表达式字段在加载期编译为 engine_expr_t。仅实现该方案所需的规格子集。
 */

#ifndef DOMAIN_PROGRAM_ENGINE_MODEL_ENGINE_MODEL_H
#define DOMAIN_PROGRAM_ENGINE_MODEL_ENGINE_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/program_engine/engine/engine_expr.h"

#include <stdbool.h>
#include <stdint.h>

/* 具名常量 */
#define ENGINE_NAME_MAX    48U /* id/信号名/通道名最大长度 */
#define ENGINE_DISPLAY_MAX 96U /* 显示名（可含中文 UTF-8） */
#define ENGINE_VERSION_MAX 16U
#define ENGINE_AFTER_MAX   4U  /* 单步骤最多前置依赖数 */

/* 阶段行进方向 */
typedef enum { ENGINE_DIR_NONE = 0, ENGINE_DIR_FORWARD, ENGINE_DIR_BACKWARD } engine_direction_t;

/* 错误策略 */
typedef enum {
    ENGINE_ERR_STOP = 0,
    ENGINE_ERR_HALT_PHASE,
    ENGINE_ERR_SKIP,
    ENGINE_ERR_DEGRADE
} engine_error_strategy_t;

/* 联锁动作 */
typedef enum { ENGINE_ILK_HALT_ALL = 0, ENGINE_ILK_HALT_PHASE, ENGINE_ILK_CUSTOM } engine_interlock_action_t;

/* 信号边沿/电平 */
typedef enum { ENGINE_EDGE_RISING = 0, ENGINE_EDGE_FALLING, ENGINE_EDGE_HIGH, ENGINE_EDGE_LOW } engine_edge_t;

/* 触发器类型（子集：condition / signal） */
typedef enum { ENGINE_TRIG_CONDITION = 0, ENGINE_TRIG_SIGNAL } engine_trigger_type_t;

/* 完成条件类型 */
typedef enum {
    ENGINE_DONE_ACTIONS_COMPLETE = 0,
    ENGINE_DONE_TRIGGER_EXIT,
    ENGINE_DONE_SIGNAL,
    ENGINE_DONE_TIMEOUT
} engine_done_type_t;

/* 步骤类型 */
typedef enum { ENGINE_STEP_EVENT = 0, ENGINE_STEP_CONTROL } engine_step_type_t;

/* 动作原语类型（子集：act 意图 / wait_time） */
typedef enum { ENGINE_ACT_INTENT = 0, ENGINE_ACT_WAIT_TIME } engine_action_type_t;

/**
 * @brief  执行机构意图（resource/cmd 等对框架不透明，由项目解释）
 */
typedef struct {
    char resource[ENGINE_NAME_MAX];
    char cmd[ENGINE_NAME_MAX];
    char dir[ENGINE_NAME_MAX]; /**< 可空 */
    int  gear;                 /**< 1 基挡位；0 常表示停 */
    char (*paths)[ENGINE_NAME_MAX];
    unsigned path_count;
    unsigned resource_id;    /**< 加载到引擎时绑定的 provider 资源 ID */
    bool     resource_bound; /**< resource_id 是否有效 */
} engine_intent_t;

/* 动作原语 */
typedef struct {
    engine_action_type_t type;
    engine_intent_t      intent; /* ENGINE_ACT_INTENT */
    uint32_t             ms;     /* wait_time: 毫秒 */
} engine_action_t;

/* 触发器 */
typedef struct {
    engine_trigger_type_t type;
    engine_expr_t        *cond;                    /* condition：编译后的表达式 */
    char                  signal[ENGINE_NAME_MAX]; /* signal：信号名 */
    engine_edge_t         edge;                    /* signal：边沿/电平 */
    unsigned              signal_id;               /**< 加载到引擎时绑定的 provider 信号 ID */
    bool                  signal_bound;            /**< signal_id 是否有效 */
} engine_trigger_t;

/* 完成条件 */
typedef struct {
    engine_done_type_t type;
    char               signal[ENGINE_NAME_MAX]; /* signal：等待的信号名 */
    int                state;                   /* signal：目标状态 */
    uint32_t           timeout_ms;              /* signal/timeout：超时（0=无） */
    uint32_t           confirm_ms;              /* signal：连续为真确认窗（0=首拍即完成） */
    unsigned           signal_id;               /**< 加载到引擎时绑定的 provider 信号 ID */
    bool               signal_bound;            /**< signal_id 是否有效 */
} engine_done_t;

/* 步骤（event / control） */
typedef struct {
    char                    id[ENGINE_NAME_MAX];
    engine_step_type_t      type;
    engine_error_strategy_t on_error;
    uint32_t                retry_max; /**< done 超时后重发 intent 次数；0=不重试 */

    /* event 型字段 */
    engine_trigger_t trigger;
    engine_expr_t   *guard; /* 可选，NULL 表示无 */
    engine_action_t *actions;
    unsigned         action_count;
    engine_done_t    done;
    char             after[ENGINE_AFTER_MAX][ENGINE_NAME_MAX];
    unsigned         after_count;

    /* control 型字段 */
    engine_expr_t  *active_while;
    engine_intent_t intent;
} engine_step_t;

/* 通道 */
typedef struct {
    char           id[ENGINE_NAME_MAX];
    engine_step_t *steps;
    unsigned       step_count;
} engine_lane_t;

/* 阶段 */
typedef struct {
    char                    id[ENGINE_NAME_MAX];
    char                    name[ENGINE_DISPLAY_MAX];
    engine_direction_t      direction;
    engine_expr_t          *entry_guard;
    engine_expr_t          *exit_guard;
    uint32_t                timeout_ms;
    engine_error_strategy_t on_timeout;
    engine_action_t        *on_enter;
    unsigned                on_enter_count;
    engine_action_t        *on_exit;
    unsigned                on_exit_count;
    /** 阶段退出时不自动释放的资源名（跨阶段保持） */
    char (*keep)[ENGINE_NAME_MAX];
    unsigned       keep_count;
    engine_lane_t *lanes;
    unsigned       lane_count;
} engine_phase_t;

/* 坐标轴（仅 physical 型） */
typedef struct {
    char   id[ENGINE_NAME_MAX];
    char   encoder[ENGINE_NAME_MAX];
    double pulse_per_mm;
    int    direction; /* 1=正向，-1=反向 */
} engine_axis_t;

/* 标记触发源：信号边沿 或 条件边沿（互斥） */
typedef enum { ENGINE_MARKER_ON_SIGNAL = 0, ENGINE_MARKER_ON_CONDITION } engine_marker_on_t;

/* 位置标记（仅 latch 型） */
typedef struct {
    char               id[ENGINE_NAME_MAX];
    char               axis[ENGINE_NAME_MAX];
    engine_marker_on_t on_kind;
    char               signal[ENGINE_NAME_MAX]; /* ON_SIGNAL */
    engine_expr_t     *cond;                    /* ON_CONDITION：编译后表达式 */
    engine_edge_t      edge;
} engine_marker_t;

/* 联锁 */
typedef struct {
    char                      id[ENGINE_NAME_MAX];
    engine_expr_t            *condition;
    engine_interlock_action_t action;
    engine_action_t          *actions; /* custom_action 的动作列表 */
    unsigned                  action_count;
    int                       priority;
    engine_expr_t            *reset_condition;
    bool                      auto_reset;
} engine_interlock_t;

/* 方案级参数 */
typedef struct {
    char   name[ENGINE_NAME_MAX]; /* 不含 $ 前缀 */
    double value;
} engine_param_t;

/* 方案（顶层） */
typedef struct {
    char schema_version[ENGINE_VERSION_MAX];
    char id[ENGINE_NAME_MAX];
    char name[ENGINE_DISPLAY_MAX];

    engine_param_t     *params;
    unsigned            param_count;
    engine_axis_t      *axes;
    unsigned            axis_count;
    engine_marker_t    *markers;
    unsigned            marker_count;
    engine_interlock_t *interlocks;
    unsigned            interlock_count;
    engine_phase_t     *phases;
    unsigned            phase_count;
} engine_program_t;

/**
 * @brief  释放方案及其全部子结构（含已编译表达式）
 * @param  prog  方案指针，可为空
 */
void engine_program_free(engine_program_t *prog);

/**
 * @brief  深拷贝方案（含已编译表达式）
 * @param  src  源方案，不可为空
 * @return 成功返回新方案；失败返回 NULL
 */
engine_program_t *engine_program_clone(const engine_program_t *src);

/* 字符串到枚举的解析辅助（供配置加载器使用，成功返回 true） */
bool engine_direction_from_str(const char *s, engine_direction_t *out);
bool engine_error_strategy_from_str(const char *s, engine_error_strategy_t *out);
bool engine_interlock_action_from_str(const char *s, engine_interlock_action_t *out);
bool engine_edge_from_str(const char *s, engine_edge_t *out);
bool engine_trigger_type_from_str(const char *s, engine_trigger_type_t *out);
bool engine_done_type_from_str(const char *s, engine_done_type_t *out);
bool engine_step_type_from_str(const char *s, engine_step_type_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROGRAM_ENGINE_MODEL_ENGINE_MODEL_H */
