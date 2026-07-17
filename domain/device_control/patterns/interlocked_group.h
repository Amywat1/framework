/**
 * @file    interlocked_group.h
 * @brief   多槽位互锁连续运动模式（直接调用 HAL，不内嵌 motor_axis）
 * @author  HUWANGWEI
 * @date    2026-07-17
 *
 * @note    核心职责是互斥管理，不是单轴生命周期复用；各槽位仅使用
 *          run_continuous / set_speed / stop / recover 等 HAL 原语。
 *          lifecycle 与过程故障回调通过 motion_lifecycle_opts_t 共享。
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_INTERLOCKED_GROUP_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_INTERLOCKED_GROUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/device_control/patterns/motion_lifecycle.h"
#include "ports/outbound/hal/motor/hal_motor_exec_port.h"

#include <stdbool.h>
#include <stdint.h>

#define INTERLOCKED_GROUP_SLOTS_MAX 4
#define INTERLOCKED_GROUP_PAIRS_MAX 4

typedef int interlocked_group_slot_id_t;

typedef struct {
    interlocked_group_slot_id_t a;
    interlocked_group_slot_id_t b;
} interlocked_group_pair_t;

typedef enum {
    INTERLOCKED_GROUP_STATE_IDLE = 0,
    INTERLOCKED_GROUP_STATE_RUNNING,
    INTERLOCKED_GROUP_STATE_STOPPING,
    INTERLOCKED_GROUP_STATE_FAULT,
} interlocked_group_state_t;

typedef struct {
    hal_motor_exec_t           *exec;
    int                         motor[INTERLOCKED_GROUP_SLOTS_MAX];
    int                         count;
    interlocked_group_pair_t    pairs[INTERLOCKED_GROUP_PAIRS_MAX];
    int                         pair_count;
    motion_lifecycle_opts_t     opts;
    bool                        inited;
} interlocked_group_t;

/**
 * @brief  初始化互锁组
 * @param[in,out] self         模式实例
 * @param[in]     exec         电机执行器句柄
 * @param[in]     motor_index  槽位到电机索引表
 * @param[in]     count        槽位数，(0, INTERLOCKED_GROUP_SLOTS_MAX]
 * @param[in]     pairs        互斥对表，pair_count 为 0 时可为 NULL
 * @param[in]     pair_count   互斥对数，[0, INTERLOCKED_GROUP_PAIRS_MAX]
 * @param[in]     opts         lifecycle 选项，可为 NULL
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法
 */
sw_err_t interlocked_group_init(interlocked_group_t            *self,
                                hal_motor_exec_t               *exec,
                                const int                      *motor_index,
                                int                             count,
                                const interlocked_group_pair_t *pairs,
                                int                             pair_count,
                                const motion_lifecycle_opts_t  *opts);

/**
 * @brief  启动或调速指定槽位（启动前停止互斥对侧）
 * @param[in,out] self        模式实例
 * @param[in]     id          槽位 ID
 * @param[in]     dir         运动方向
 * @param[in]     speed_gear  1 基速度挡位（1..N）
 * @return SW_OK 成功；SW_ERR_NOT_INIT / SW_ERR_PARAM / SW_ERR_STATE 失败
 */
sw_err_t interlocked_group_start(interlocked_group_t        *self,
                                 interlocked_group_slot_id_t id,
                                 hal_motor_dir_t             dir,
                                 int                         speed_gear);

/**
 * @brief  停止指定槽位
 * @param[in,out] self 模式实例
 * @param[in]     id   槽位 ID
 * @return SW_OK 成功；SW_ERR_NOT_INIT / SW_ERR_PARAM 失败
 */
sw_err_t interlocked_group_stop(interlocked_group_t *self, interlocked_group_slot_id_t id);

/**
 * @brief  停止全部槽位
 * @param[in,out] self 模式实例
 * @return SW_OK 成功；SW_ERR_NOT_INIT 失败
 */
sw_err_t interlocked_group_stop_all(interlocked_group_t *self);

/**
 * @brief  查询槽位状态
 * @param[in] self 模式实例
 * @param[in] id   槽位 ID
 * @return 槽位状态；非法时返回 IDLE
 */
interlocked_group_state_t interlocked_group_state(const interlocked_group_t *self, interlocked_group_slot_id_t id);

/**
 * @brief  查询槽位当前方向
 * @param[in] self 模式实例
 * @param[in] id   槽位 ID
 * @return 当前方向；非法时返回 FORWARD
 */
hal_motor_dir_t interlocked_group_direction(const interlocked_group_t *self, interlocked_group_slot_id_t id);

/**
 * @brief  查询槽位故障码
 * @param[in] self 模式实例
 * @param[in] id   槽位 ID
 * @return 故障码；非法或无故障时为 HAL_MOTOR_FAULT_NONE
 */
hal_motor_fault_code_t interlocked_group_fault_code(const interlocked_group_t *self, interlocked_group_slot_id_t id);

/**
 * @brief  对指定槽位执行恢复步骤
 * @param[in,out] self 模式实例
 * @param[in]     id   槽位 ID
 * @param[in]     step 恢复步骤
 * @return SW_OK 成功；SW_ERR_NOT_INIT / SW_ERR_PARAM / SW_ERR_STATE 失败
 */
sw_err_t interlocked_group_recover(interlocked_group_t         *self,
                                   interlocked_group_slot_id_t  id,
                                   hal_motor_recovery_step_t    step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_INTERLOCKED_GROUP_H */
