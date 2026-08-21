/**
 * @file motor_executor.h
 * @brief 电机执行器组合件 —— 运动状态机与硬件端口注入接口。
 *
 * 与具体机型/厂商无关：所有硬件操作经端口函数指针表注入，计时经 motor_clock_t。
 * 领域层以编译期定长槽池持有全部运行时状态。项目 bindings 只提交
 * slot ID、配置与硬件端口，随后保存返回的 motor_exec_t * 并注入机构模式。
 *
 * 量纲约定：速度频率单位 0.01Hz(厘赫)；时间单位 ms；位置单位 脉冲；
 * 电流由硬件端口实现约定。执行器与配置采用编译期上限的定长存储，不做动态内存分配。
 */
#ifndef DOMAIN_MECHANISM_MOTOR_MOTOR_EXECUTOR_H
#define DOMAIN_MECHANISM_MOTOR_MOTOR_EXECUTOR_H

#include "common/sw_error.h"
#include "domain/ports/outbound/motor/motor_exec_port.h"
#include "domain/ports/outbound/motor/motor_hw_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- 编译期容量上限 ------------------------- */
/*
 * MOTOR_MAX_MOTORS / DRIVERS / INTERLOCKS = 16：覆盖当前隧道/龙门多轴接入余量
 * （输送、侧刷、顶刷、风机、行车/升降等逻辑轴合计），并控制静态 RAM。
 * 互锁条数与轴数同级。
 */
#define MOTOR_MAX_MOTORS     16 /**< 单执行器最多管理的电机数 */
#define MOTOR_MAX_DRIVERS    16 /**< 单执行器最多管理的物理驱动器数 */
#define MOTOR_MAX_INTERLOCKS 16 /**< 互锁规则条数上限 */

/** 项目可通过编译定义设置执行器实例数；每个实例静态占用一个完整执行器槽位。 */
#ifndef WDF_MOTOR_EXECUTOR_INSTANCE_COUNT
#define WDF_MOTOR_EXECUTOR_INSTANCE_COUNT 1
#endif

#if WDF_MOTOR_EXECUTOR_INSTANCE_COUNT < 1
#error "WDF_MOTOR_EXECUTOR_INSTANCE_COUNT must be at least 1"
#endif

/* 回原点默认低速频率（配置未提供 slowFreq 时使用），单位厘赫 */
#define MOTOR_HOME_DEFAULT_FREQ_CENTI_HZ 100
/* 编码器同步清零最大重试次数 */
#define MOTOR_ZERO_MAX_TRIES             3

/* ------------------------- 配置 ------------------------- */

/** @brief 单电机监测项配置（各项独立，未启用不参与判定）。 */
typedef struct {
    bool monitor_current;  /**< 是否启用负载电流监测 */
    int  cur_max_accel;    /**< 加速段电流上限 */
    int  cur_min_accel;    /**< 加速段电流下限 */
    int  cur_max_steady;   /**< 匀速段电流上限 */
    int  cur_min_steady;   /**< 匀速段电流下限 */
    int  cur_confirm_ms;   /**< 越限持续确认门限 */
    int  startup_delay_ms; /**< 启动后延迟再启用电流判定 */

    bool monitor_feedback; /**< 是否启用运行反馈监测 */
    int  fb_confirm_ms;    /**< 反馈异常单次确认时间 */
    int  fb_retries;       /**< 瞬态自动重试次数（发作次数 > 此值才判故障） */

    bool monitor_temp;     /**< 是否启用温度监测 */
    int  temp_max;         /**< 温度上限 */
    int  temp_confirm_ms;  /**< 温度确认门限 */

    bool monitor_voltage;  /**< 是否启用母线电压监测 */
    int  volt_min;         /**< 电压下限 */
    int  volt_confirm_ms;  /**< 电压确认门限 */
} motor_monitor_cfg_t;

/** @brief 单电机配置。 */
typedef struct {
    int                  driver_index; /**< 指向哪个物理驱动器 */
    bool                 has_encoder;  /**< 是否配置编码器 */
    motor_encoder_kind_t encoder_kind; /**< 编码器语义；无编码器时忽略 */

    int cooldown_ms;                   /**< 停机冷却期 */
    int reversal_stop_ms;              /**< 方向切换停止时间 */
    int accel_ms;                      /**< 电流监测加速段时长（ms） */

    int pos_tolerance;                 /**< 到位容差（脉冲） */
    int decel_point;                   /**< 定位降速点（距目标脉冲数，0=不启用） */
    int position_slow_gear;            /**< 挡位定位进入减速区后的挡位，0=不切换 */
    int slow_freq;                     /**< 定位低速段频率 */

    bool prep_required;                /**< 启动前需预备动作 */

    bool    has_soft_limit;            /**< 是否配置软限位 */
    int64_t soft_min;                  /**< 软限位下限（脉冲） */
    int64_t soft_max;                  /**< 软限位上限（脉冲） */

    int default_max_time_ms;           /**< 运行默认超时兜底（必须 > 0） */

    int  enc_stall_ticks;              /**< 连续多拍无变化→告警（0=不检测） */
    int  enc_jump_max;                 /**< 单拍跳变上限→告警（0=不检测） */
    bool enc_escalate;                 /**< 编码器告警升级为故障 */

    int gear_count;                    /**< 可用挡位数；执行器仅校验范围，不转换频率 */

    motor_monitor_cfg_t mon;           /**< 监测项配置 */
} motor_motor_cfg_t;

/** @brief 互锁类型。 */
typedef enum {
    MOTOR_INTERLOCK_MUTEX = 0,      /**< 互斥：b 运行相关态时 a 不可启动 */
    MOTOR_INTERLOCK_PREREQ_POSITION /**< 前置位置：a 启动需 b 处于位置区间 */
} motor_interlock_kind_t;

/** @brief 互锁规则。 */
typedef struct {
    motor_interlock_kind_t kind;    /**< 互锁类型 */
    int                    a;       /**< 受约束电机 */
    int                    b;       /**< 关联电机 */
    int64_t                pos_min; /**< PrereqPosition: b 需 >= 此值 */
    int64_t                pos_max; /**< PrereqPosition: b 需 <= 此值 */
} motor_interlock_t;

/** @brief 执行器配置。 */
typedef struct {
    motor_motor_cfg_t motors[MOTOR_MAX_MOTORS];         /**< 各电机配置 */
    int               motor_count;                      /**< 电机数 */
    motor_interlock_t interlocks[MOTOR_MAX_INTERLOCKS]; /**< 互锁规则 */
    int               interlock_count;                  /**< 互锁条数 */
    int               driver_count;                     /**< 物理驱动器数 */
    int               watchdog_ms;                      /**< tick 缺拍阈值（>0） */
    int               tick_ms;                          /**< 标称 tick 周期（>0） */
} motor_config_t;

/** @brief 初始化结果。 */
typedef struct {
    bool        ok;
    const char *error; /**< 失败原因（静态字符串），ok 时为空串 */
} motor_init_result_t;

/* ------------------------- 事件 ------------------------- */

/** @brief 事件回调；载荷即端口 `motor_event_t`。ctx 为注册时透传的上下文。 */
typedef void (*motor_event_cb_t)(const motor_event_t *ev, void *ctx);

/* ------------------------- 公共 API ------------------------- */

/**
 * @brief 把项目配置绑定到领域静态槽位，fail-fast 校验并归零状态。
 * @param slot_id 项目选择的稳定槽位 ID，范围 [0, WDF_MOTOR_EXECUTOR_INSTANCE_COUNT)。
 * @param cfg   配置（按值拷贝进 exec）。
 * @param ports 端口集合（按值拷贝，内部数组由调用方保证生命周期）。
 * @param out_exec 返回可注入机构模式的不透明句柄。
 * @return ok=true 表示可运行；否则 error 指向失败原因。
 * @note 每个槽位只允许在启动装配阶段绑定一次，运行期不释放。
 */
motor_init_result_t motor_executor_bind(unsigned              slot_id,
                                        const motor_config_t *cfg,
                                        const motor_ports_t  *ports,
                                        motor_exec_t    **out_exec);

/**
 * @brief 重新初始化（致命错误后恢复），沿用首次 motor_executor_bind 的 cfg/ports。
 */
motor_init_result_t motor_executor_reinit(motor_exec_t *exec);

/**
 * @brief 周期处理。须按 cfg.tick_ms 节拍调用。
 * @note 相邻两次调用间隔超过 watchdog_ms 将进入安全态并切断输出。
 */
void motor_executor_tick(motor_exec_t *exec);

/** @brief 手动清零编码器（同步硬件计数器，失败自动重试）。 */
motor_cmd_result_t motor_executor_zero_encoder(motor_exec_t *exec, int motor);

/** @brief 上层显式确认位置基准可信。 */
motor_cmd_result_t motor_executor_confirm_baseline(motor_exec_t *exec, int motor);

/** @brief 急停解除后显式复位。 */
void motor_executor_reset_estop(motor_exec_t *exec);

/** @brief 看门狗恢复正常节拍后复位。 */
void motor_executor_reset_watchdog(motor_exec_t *exec);

/**
 * @brief 查询电机当前输出频率。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 当前给定频率（厘赫）；输出关断或电机号越界时为 0。
 */
int motor_executor_current_freq(const motor_exec_t *exec, int motor);

/**
 * @brief 查询执行器是否处于看门狗安全态。
 * @param exec 已完成初始化的执行器。
 * @return true 表示 tick 缺拍已触发安全锁定，所有电机已切断输出；
 *         须调用 motor_executor_reset_watchdog 方可恢复。
 */
bool motor_executor_in_safe_state(const motor_exec_t *exec);

/** @brief 注册事件回调；ctx 在回调时透传。回调运行于 tick 上下文，回调内不得下发运动指令。 */
void motor_executor_set_event_callback(motor_exec_t *exec, motor_event_cb_t cb, void *ctx);

#ifdef MOTOR_EXECUTOR_UNIT_TEST
/** @brief 单元测试隔离用：清空全部静态槽位。 */
void motor_executor_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MECHANISM_MOTOR_MOTOR_EXECUTOR_H */
