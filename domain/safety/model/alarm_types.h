/**
 * @file    alarm_types.h
 * @brief   报警码、等级、清除方式与定义类型
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef DOMAIN_SAFETY_MODEL_ALARM_TYPES_H
#define DOMAIN_SAFETY_MODEL_ALARM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 报警码编码：6 位十进制 = 大类(1) + 编号(3) + 性质(2)
 * ------------------------------------------------------------------------- */
#define ALARM_CODE_CATEGORY_MIN 1U
#define ALARM_CODE_CATEGORY_MAX 9U
#define ALARM_CODE_INDEX_MAX    999U
#define ALARM_CODE_NATURE_MAX   99U

#define ALARM_CODE_MAKE(category, index, nature) ((uint32_t)((category) * 100000U + (index) * 100U + (nature)))

#define ALARM_CODE_CATEGORY(code) ((uint32_t)((code) / 100000U))
#define ALARM_CODE_INDEX(code)    ((uint32_t)(((code) / 100U) % 1000U))
#define ALARM_CODE_NATURE(code)   ((uint32_t)((code) % 100U))

#define ALM_C_POWER 1U
#define ALM_C_SENSE 2U
#define ALM_C_ACT   3U
#define ALM_C_CTRL  4U
#define ALM_C_SW    9U

#define ALM_N_OTHER     0U
#define ALM_N_OVERLOAD  1U
#define ALM_N_COMM_LOST 2U
#define ALM_N_HW_FAULT  3U
#define ALM_N_SIG_ERR   4U
#define ALM_N_TIMEOUT   5U
#define ALM_N_PRESSURE  6U
#define ALM_N_LEVEL     7U
#define ALM_N_SAFETY    9U

/* -------------------------------------------------------------------------
 * 容量依据（按已接入项目实测）
 *
 * | 项目           | 实测值  | 容量 | 说明 |
 * |----------------|---------|------|------|
 * | 报警目录条目     | 32（DI 12 + 通讯 4 + 软件 16） | 64 | 留一倍余量给新增机型报警 |
 * | 同时活跃报警     | 远小于目录规模 | 32 | 上限取目录一半；lockout 可驱逐非 lockout 腾槽 |
 * | 会话日志条目     | 单次洗车通常 0~2 | 16 | 只记 MAJOR 及以上且同码去重；满则累计丢弃计数 |
 *
 * 活跃池满时：非 lockout 返回 SW_ERR_OVERFLOW；lockout 等级驱逐一条非 lockout
 * （CLEARED + ERROR 日志）后再插入。32 条都是 lockout 时仍硬失败。
 * 不静默丢弃 lockout——漏报 CRITICAL 比丢掉 MINOR 危险。
 *
 * 变位在放锁后直接 `event_publish_required`，不另设待发队列。本轮待发布条数
 * 不超过活动表规模，走事件总线既有容量与丢件契约。
 * ------------------------------------------------------------------------- */
#define ALARM_DESC_MAX            48U
#define ALARM_CATALOG_MAX         64U
#define ALARM_ACTIVE_MAX          32U
#define ALARM_SESSION_JOURNAL_MAX 16U

/* -------------------------------------------------------------------------
 * 安全姿态
 * ------------------------------------------------------------------------- */
typedef enum {
    SAFETY_POSTURE_NOMINAL = 0,
    SAFETY_POSTURE_LOCKOUT,
} safety_posture_t;

/* -------------------------------------------------------------------------
 * 报警等级与清除策略
 * ------------------------------------------------------------------------- */
typedef enum {
    ALARM_LEVEL_MINOR = 0, /**< 仅记录，不挡开洗 */
    ALARM_LEVEL_MAJOR,     /**< 禁开洗；洗中不中断；洗完仍活跃则进 STOPPED */
    ALARM_LEVEL_CRITICAL,  /**< LOCKOUT：立刻停机 */
} alarm_level_t;

typedef enum {
    ALARM_CLEAR_AUTO_STATIC = 0,
    ALARM_CLEAR_ON_MOTION,
    ALARM_CLEAR_MANUAL_RESET,
} alarm_clear_t;

/** @brief ON_MOTION 重评估分组 ID；framework 只定义 NONE，具体取值由项目配置 */
#define ALARM_REEVAL_GROUP_NONE 0U
typedef uint16_t motion_reeval_group_id_t;

/** @brief ON_MOTION 重评估触发源类型 */
typedef enum {
    ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED = 1,
    ALARM_REEVAL_TRIGGER_WASH_CHECKPOINT    = 2,
} alarm_reeval_trigger_kind_t;

/** @brief 一条 ON_MOTION 重评估绑定（触发源 → 重评估分组） */
typedef struct {
    alarm_reeval_trigger_kind_t kind;
    uint16_t                    trigger_id;
    motion_reeval_group_id_t    group;
} alarm_reeval_binding_t;

/** @brief 空报警码；用于「当前无最高级别告警」等占位语义 */
#define ALARM_CODE_NONE 0U

static inline bool alarm_code_parts_valid(uint32_t category, uint32_t index, uint32_t nature)
{
    return (category >= ALARM_CODE_CATEGORY_MIN) && (category <= ALARM_CODE_CATEGORY_MAX)
           && (index <= ALARM_CODE_INDEX_MAX) && (nature <= ALARM_CODE_NATURE_MAX);
}

static inline bool alarm_code_is_valid(uint32_t code)
{
    uint32_t category;
    uint32_t index;
    uint32_t nature;

    if (code == ALARM_CODE_NONE) {
        return false;
    }

    category = ALARM_CODE_CATEGORY(code);
    index    = ALARM_CODE_INDEX(code);
    nature   = ALARM_CODE_NATURE(code);

    return alarm_code_parts_valid(category, index, nature) && (code == ALARM_CODE_MAKE(category, index, nature));
}

static inline bool alarm_code_make_checked(uint32_t category, uint32_t index, uint32_t nature, uint32_t *out_code)
{
    if (!alarm_code_parts_valid(category, index, nature) || (out_code == NULL)) {
        return false;
    }
    *out_code = ALARM_CODE_MAKE(category, index, nature);
    return true;
}

typedef struct {
    uint32_t                 code;
    alarm_level_t            level;
    alarm_clear_t            clear;
    motion_reeval_group_id_t reeval_group;
    char                     desc[ALARM_DESC_MAX];
} alarm_def_t;

/**
 * @brief 活动告警实例。
 *
 * @note  `level` / `clear` / `reeval_group` 是插入时从目录定义拷入的快照。
 *        目录在运行期不变（`load_catalog` 会同时清空活动表），因此活动表上的
 *        任何判定都不需要回查目录——`reevaluate_group` / `reset_all` / `clear`
 *        据此只扫活动表一趟。
 */
typedef struct {
    uint32_t                 code;             /**< 告警码。 */
    alarm_level_t            level;            /**< 告警等级。 */
    alarm_clear_t            clear;            /**< 清除策略。 */
    motion_reeval_group_id_t reeval_group;     /**< ON_MOTION 重评估分组。 */
    uint64_t                 triggered_at_ms;  /**< 首次触发时间。 */
    bool                     condition_active; /**< 故障源当前是否仍成立。 */
} alarm_instance_t;

/**
 * @brief  一次持锁读出的安全投影
 * @note   活动表、blocking、最高码、姿态与会话 journal 来自同一时刻。
 *         journal 不参与开洗拒绝或洗后 STOPPED 判定。
 */
typedef struct {
    alarm_instance_t list[ALARM_ACTIVE_MAX];                         /**< 活动告警拷贝 */
    unsigned         count;                                          /**< list 有效条数 */
    bool             blocking;                                       /**< 存在阻塞开洗的等级 */
    uint32_t         top_code;                                       /**< 当前最高等级告警码；无则 ALARM_CODE_NONE */
    safety_posture_t posture;                                        /**< 由 CRITICAL 推导的安全姿态 */
    uint32_t         session_journal[ALARM_SESSION_JOURNAL_MAX];     /**< 本会话 MAJOR+ 码 */
    unsigned         journal_count;                                  /**< journal 有效条数 */
    uint32_t         journal_dropped;                                /**< 满池未记入的累计条数，读不清零 */
} alarm_safety_view_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_MODEL_ALARM_TYPES_H */
