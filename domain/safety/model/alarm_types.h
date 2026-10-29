/**
 * @file    alarm_types.h
 * @brief   报警码、等级、生命周期与定义类型
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
 * 报警码编码：6 位十进制 = 对象层(1) + 对象号(3) + 故障形态(2)
 * 框架统一约定，各项目共用；alarm_registry_load_catalog 拒绝不符合格式的目录。
 * 对象层：1 动力设备  2 检测元件  3 开关元件  4 控制装置  9 逻辑事件
 * 故障形态：0 未分类  1 过载  2 通讯中断  3 欠载  4 信号异常
 *           5 动作超时  6 故障反馈  7 液位异常  8 压力异常  9 安全触发
 *          10 主动停止  11 检测停用
 * ------------------------------------------------------------------------- */
#define ALARM_CODE_LAYER_MIN      1U
#define ALARM_CODE_LAYER_MAX      9U
#define ALARM_CODE_OBJECT_MAX     999U
#define ALARM_CODE_FAULT_MODE_MAX 99U

#define ALARM_CODE_MAKE(layer, object_id, fault_mode) \
    ((uint32_t)((layer) * 100000U + (object_id) * 100U + (fault_mode)))

#define ALARM_CODE_LAYER(code)      ((uint32_t)((code) / 100000U))
#define ALARM_CODE_OBJECT(code)     ((uint32_t)(((code) / 100U) % 1000U))
#define ALARM_CODE_FAULT_MODE(code) ((uint32_t)((code) % 100U))

#define ALM_C_POWER  1U /* 动力设备：将电能转为机械功的负载 */
#define ALM_C_DETECT 2U /* 检测元件：仅提供检测输入的器件 */
#define ALM_C_SWITCH 3U /* 开关元件：仅负责回路通断的元件 */
#define ALM_C_CTRL   4U /* 控制装置：具备控制或通讯能力的装置 */
#define ALM_C_LOGIC  9U /* 逻辑事件：无对应硬件对象的软件故障 */

#define ALM_N_UNCLASSIFIED    0U /* 未分类：无对应发现方式（如未识别的运行状态） */
#define ALM_N_OVERLOAD        1U /* 过载：过流、过载接点 */
#define ALM_N_COMM_INTERRUPT  2U /* 通讯中断 */
#define ALM_N_UNDERLOAD       3U /* 欠载：空载、电流过低 */
#define ALM_N_SIG_ERR         4U /* 信号异常：传感器失效或读数不可信 */
#define ALM_N_ACTION_TIMEOUT  5U /* 动作超时 */
#define ALM_N_FAULT_FEEDBACK  6U /* 故障反馈：对象自身故障接点 */
#define ALM_N_LEVEL_ERR       7U /* 液位异常：液位测量值越限 */
#define ALM_N_PRESSURE_ERR    8U /* 压力异常：压力测量值越限 */
#define ALM_N_SAFETY_TRIGGER  9U /* 安全触发：保护事件，非设备损坏 */
#define ALM_N_OPERATOR_STOP   10U /* 主动停止：人下发的中止或全切断，不是设备损坏 */
#define ALM_N_OUT_OF_SERVICE  11U /* 检测停用：该对象发现路径已旁路，补偿码由装载期合成 */

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
#define ALARM_OOS_MAX             8U
#define ALARM_SHELVE_MAX_MS       (4U * 3600U * 1000U) /**< 搁置时限封顶：4 小时 */

/* -------------------------------------------------------------------------
 * 安全姿态
 * ------------------------------------------------------------------------- */
typedef enum {
    SAFETY_POSTURE_NOMINAL = 0,
    SAFETY_POSTURE_LOCKOUT,
} safety_posture_t;

/* -------------------------------------------------------------------------
 * 报警等级与生命周期
 * ------------------------------------------------------------------------- */
typedef enum {
    ALARM_LEVEL_MINOR = 0, /**< 仅记录，不挡开洗 */
    ALARM_LEVEL_MAJOR,     /**< 禁开洗；洗中不中断；洗完仍活跃则进 STOPPED */
    ALARM_LEVEL_CRITICAL,  /**< LOCKOUT：立刻停机 */
} alarm_level_t;

/**
 * @brief  报警生命周期
 * @note   只分两档。运动结案不是第三档，而是锁存上的可选 `reeval_group`。
 */
typedef enum {
    ALARM_LIFECYCLE_FOLLOW_SIGNAL = 0, /**< 条件消失即结案 */
    ALARM_LIFECYCLE_LATCH,             /**< 条件消失仍锁存，由运动组或复位结案 */
} alarm_lifecycle_t;

/**
 * @brief  提示裁决用的整机工况
 * @note   数值必须与 `operational_mode_t` 一致。safety 不包含 op_mode 头，由应用桥传入。
 */
typedef enum {
    ALARM_OP_INIT = 0,
    ALARM_OP_STOPPED,
    ALARM_OP_STANDBY,
    ALARM_OP_ACTIVE,
    ALARM_OP_CLEARING,
    ALARM_OP_HOLD,
    ALARM_OP_SELF_CHECK,
    ALARM_OP_RECOVERING,
} alarm_op_mode_t;

/** @brief 八态抑制位；STANDBY/ACTIVE/HOLD 装载期拒绝 */
#define ALARM_SUPPRESS_MODE_BIT(mode) ((uint8_t)(1U << (unsigned)(mode)))

/**
 * @brief  设备开停要求
 * @note   缺省 ANY。装载期对超时/欠载若仍为 ANY，则升为 COMMANDED。
 */
typedef enum {
    ALARM_NEED_RUN_ANY = 0,       /**< 任何 run_state 都可提示 */
    ALARM_NEED_RUN_COMMANDED,     /**< 仅受令时提示过程量 */
} alarm_need_run_t;

/** @brief 对象运行态；未推送视为 IDLE */
typedef enum {
    ALARM_OBJECT_RUN_IDLE = 0,    /**< 策略未开 */
    ALARM_OBJECT_RUN_COMMANDED,   /**< 正在受令 */
    ALARM_OBJECT_RUN_TRIPPED,     /**< 因故障停 */
} alarm_object_run_t;

/**
 * @brief  报警互挡角色
 * @note   FROM_FAULT 在运行期按故障形态推导。
 */
typedef enum {
    ALARM_ECLIPSE_FROM_FAULT = 0, /**< 按故障形态：过载/安全/主动停止/信号异常为根因 */
    ALARM_ECLIPSE_ROOT,           /**< 根因，不被同组互挡 */
    ALARM_ECLIPSE_CONSEQUENTIAL,  /**< 次生，可被根因、切断链或本对象 TRIPPED 挡住 */
} alarm_eclipse_role_t;

/**
 * @brief  搁置策略
 * @note   DEFAULT：MINOR/MAJOR 允许，CRITICAL 与安全触发禁止。
 */
typedef enum {
    ALARM_SHELVE_DEFAULT = 0,
    ALARM_SHELVE_ALLOW,
    ALARM_SHELVE_FORBID,
} alarm_shelve_policy_t;

/** @brief 检测停用后的开洗后果 */
typedef enum {
    ALARM_OOS_ADMIT_BLOCK = 0, /**< 补偿码 MAJOR，暂停接单 */
    ALARM_OOS_ADMIT_RUN,       /**< 补偿码 MINOR，继续开洗 */
} alarm_oos_admit_t;

/** @brief 当前不提示的原因；无则 announced 为真 */
typedef enum {
    ALARM_MUTE_NONE = 0,
    ALARM_MUTE_MODE,     /**< 装置工况 */
    ALARM_MUTE_RUN,      /**< 设备开停 */
    ALARM_MUTE_ECLIPSE,  /**< 报警互挡 */
    ALARM_MUTE_SHELVED,  /**< 搁置 */
} alarm_mute_reason_t;

/**
 * @brief  对象键：层 × 1000 + 对象号，避免跨层对象号碰撞
 */
static inline uint32_t alarm_object_key(uint32_t layer, uint32_t object_id)
{
    return (layer * 1000U) + object_id;
}

static inline uint32_t alarm_code_object_key(uint32_t code)
{
    return alarm_object_key(ALARM_CODE_LAYER(code), ALARM_CODE_OBJECT(code));
}

/** @brief 锁存报警的运动结案分组 ID；framework 只定义 NONE，具体取值由项目配置 */
#define ALARM_REEVAL_GROUP_NONE 0U
typedef uint16_t motion_reeval_group_id_t;

/** @brief 运动结案重评估触发源类型 */
typedef enum {
    ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED = 1,
    ALARM_REEVAL_TRIGGER_WASH_CHECKPOINT    = 2,
} alarm_reeval_trigger_kind_t;

/** @brief 一条运动结案绑定（触发源 → 重评估分组） */
typedef struct {
    alarm_reeval_trigger_kind_t kind;
    uint16_t                    trigger_id;
    motion_reeval_group_id_t    group;
} alarm_reeval_binding_t;

/** @brief 空报警码；用于「当前无最高级别告警」等占位语义 */
#define ALARM_CODE_NONE 0U

static inline bool alarm_code_parts_valid(uint32_t layer, uint32_t object_id, uint32_t fault_mode)
{
    return (layer >= ALARM_CODE_LAYER_MIN) && (layer <= ALARM_CODE_LAYER_MAX)
           && (object_id <= ALARM_CODE_OBJECT_MAX) && (fault_mode <= ALARM_CODE_FAULT_MODE_MAX);
}

static inline bool alarm_code_is_valid(uint32_t code)
{
    uint32_t layer;
    uint32_t object_id;
    uint32_t fault_mode;

    if (code == ALARM_CODE_NONE) {
        return false;
    }

    layer      = ALARM_CODE_LAYER(code);
    object_id  = ALARM_CODE_OBJECT(code);
    fault_mode = ALARM_CODE_FAULT_MODE(code);

    return alarm_code_parts_valid(layer, object_id, fault_mode)
           && (code == ALARM_CODE_MAKE(layer, object_id, fault_mode));
}

static inline bool alarm_code_make_checked(uint32_t layer, uint32_t object_id, uint32_t fault_mode, uint32_t *out_code)
{
    if (!alarm_code_parts_valid(layer, object_id, fault_mode) || (out_code == NULL)) {
        return false;
    }
    *out_code = ALARM_CODE_MAKE(layer, object_id, fault_mode);
    return true;
}

typedef struct {
    uint32_t                 code;
    alarm_level_t            level;
    alarm_lifecycle_t        lifecycle;
    motion_reeval_group_id_t reeval_group;
    char                     desc[ALARM_DESC_MAX];
    alarm_need_run_t         need_run;       /**< 缺省 ANY；超时/欠载装载期升为 COMMANDED */
    alarm_eclipse_role_t     eclipse_role;   /**< 缺省按故障形态推导 */
    alarm_shelve_policy_t    shelve_policy;  /**< 缺省按等级 */
    bool                     allow_oos;      /**< 是否允许检测停用 */
    alarm_oos_admit_t        oos_admit;      /**< 停用后开洗后果；仅 allow_oos 有意义 */
    uint8_t                  suppress_modes; /**< 八态抑制位图；STANDBY/ACTIVE/HOLD 装载拒绝 */
} alarm_def_t;

/**
 * @brief 活动告警实例。
 *
 * @note  `level` / `lifecycle` / `reeval_group` 是插入时从目录定义拷入的快照。
 *        提示字段在运行期由 registry 按三轴谓词重算，不回写目录。
 *        目录在运行期不变（`load_catalog` 会同时清空活动表），因此结案判定
 *        仍只扫活动表一趟。
 */
typedef struct {
    uint32_t                 code;             /**< 告警码。 */
    alarm_level_t            level;            /**< 告警等级。 */
    alarm_lifecycle_t        lifecycle;        /**< 生命周期。 */
    motion_reeval_group_id_t reeval_group;     /**< 锁存的运动结案分组；无则为 NONE。 */
    uint64_t                 triggered_at_ms;  /**< 首次触发时间。 */
    bool                     condition_active; /**< 故障源当前是否仍成立。 */
    bool                     announced;        /**< 是否向人提示；不影响开洗闸 */
    alarm_mute_reason_t      mute_reason;      /**< 不提示时的原因；announced 时为 NONE */
    uint64_t                 shelved_until_ms; /**< 搁置截止；0 表示未搁 */
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
    uint32_t         oos_list[ALARM_OOS_MAX];                        /**< 当前检测停用的原码 */
    unsigned         oos_count;                                      /**< oos_list 有效条数 */
} alarm_safety_view_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_MODEL_ALARM_TYPES_H */
