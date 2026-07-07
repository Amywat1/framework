/**
 * @file    brush.h
 * @brief   刷子机构领域层接口。
 *
 * 本模块管理一组共享同一 hal_motor_exec_t 的刷子槽位，每个槽位由项目层在
 * brush_init() 时分配电机索引。刷子之间是否互斥由 brush_interlock_pair_t
 * 配置注入决定：声明为互锁对的两个槽位，任一方启动前会自动停止另一方；未出
 * 现在互锁对中的槽位互不影响，可同时运行。互锁对是否会真正生效由项目层的
 * hal_motor_exec 配置（MOTOR_INTERLOCK_MUTEX）提供硬保护，本模块只负责在此
 * 之上按配置主动发出停止指令，不重新实现互斥保护本身。
 *
 * motor_tick() 由机型层统一调度，调用方无需另行调用。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_BRUSH_H
#define DOMAIN_DEVICE_MECHANISM_BRUSH_H

#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- 类型定义 ----------------------- */

/** @brief 框架支持的刷子槽位数上限。 */
#define BRUSH_MAX_COUNT      4

/** @brief 互锁对数量上限。 */
#define BRUSH_MAX_INTERLOCKS 4

/** @brief 刷子标识，项目层分配的槽位索引（0..count-1）。 */
typedef int brush_id_t;

/**
 * @brief 互锁对：a 与 b 互斥。
 *
 * 启动 a 前会先停止 b，反之亦然；未出现在任何互锁对中的槽位互不影响。
 */
typedef struct {
    brush_id_t a; /**< 互锁槽位 A */
    brush_id_t b; /**< 互锁槽位 B */
} brush_interlock_pair_t;

/**
 * @brief 刷子槽位状态，由该槽位对应电机的 hal_motor_phase_t 映射而来。
 */
typedef enum {
    BRUSH_STATE_IDLE = 0, /**< 空闲：电机已停止 */
    BRUSH_STATE_RUNNING,  /**< 运行中 */
    BRUSH_STATE_STOPPING, /**< 减速停止中 */
    BRUSH_STATE_FAULT,    /**< 故障，需调用 hal_motor_recover() 恢复对应电机 */
} brush_state_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化刷子模块。
 *
 * @param exec            共享 motor_executor_t，须已完成 motor_init。
 * @param motor_index     各刷子槽位对应的电机索引数组，按槽位顺序排列。
 * @param count           刷子槽位数量，须不超过 BRUSH_MAX_COUNT。
 * @param interlocks      互锁对配置数组，可为 NULL（表示无互锁，全部独立）。
 * @param interlock_count 互锁对数量，须不超过 BRUSH_MAX_INTERLOCKS。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法或数量越界。
 */
sw_err_t brush_init(hal_motor_exec_t *exec,
                    const int *motor_index, int count,
                    const brush_interlock_pair_t *interlocks, int interlock_count);

/**
 * @brief 命令指定刷子运行（异步）。
 *
 * 若目标刷子当前未运行，将先停止与其互锁的其余槽位（若在运行），再下发启动
 * 命令；若目标刷子已在运行，则仅调速。不在任何互锁对中的槽位不受影响。
 *
 * @param id        目标刷子标识。
 * @param speed_gear 速度挡位，对应 motor_config_t 中的 gear_freq 索引。
 * @return SW_OK        命令已受理。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_PARAM    参数非法。
 *         SW_ERR_STATE    目标槽位当前处于故障态，须先恢复。
 */
sw_err_t brush_start(brush_id_t id, int speed_gear);

/**
 * @brief 命令停止指定刷子（异步）。
 *
 * 不检查故障状态，即使目标槽位处于故障态也会下发停止指令。
 *
 * @return SW_OK        命令已受理。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_PARAM    参数非法。
 */
sw_err_t brush_stop(brush_id_t id);

/**
 * @brief 命令停止全部已配置刷子（异步）。
 *
 * 不检查故障状态，对每个槽位尽力下发停止指令；供安全/急停路径调用。
 *
 * @return SW_OK        命令已受理。
 *         SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t brush_stop_all(void);

/**
 * @brief 查询指定刷子槽位当前状态。
 */
brush_state_t brush_state(brush_id_t id);

/**
 * @brief 查询指定刷子槽位的底层电机故障码（仅在 BRUSH_STATE_FAULT 时有实质意义）。
 */
hal_motor_fault_code_t brush_fault_code(brush_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_BRUSH_H */
