/**
 * @file    brush.h
 * @brief   刷子机构领域层接口。
 *
 * 侧刷与顶刷共用一台变频器，通过接触器切换选中对象。这个切换时序完全由
 * MCC 管理：侧刷/顶刷各占用一个 hal_motor_exec_t 电机槽位，两者共享同一组
 * VFD 驱动函数，仅 motor_driver_t::prepare 不同（各自负责切换到自己对应的
 * 接触器），并以 MOTOR_INTERLOCK_MUTEX 双向互锁保证二者不会同时运行。
 * 本模块只做"决定控哪个电机"的映射，不再自建状态机。
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

/** @brief 刷子标识。 */
typedef enum {
    BRUSH_SIDE = 0, /**< 侧刷 */
    BRUSH_TOP  = 1, /**< 顶刷 */
    BRUSH_ID_MAX
} brush_id_t;

/**
 * @brief 刷子模块整体状态，由侧刷/顶刷两个电机槽位的 hal_motor_phase_t 聚合而来。
 *
 * 优先级：FAULT > RUNNING > STOPPING > IDLE（任一槽位故障即报故障）。
 */
typedef enum {
    BRUSH_STATE_IDLE = 0, /**< 空闲：两路电机均已停止 */
    BRUSH_STATE_RUNNING,  /**< 运行中 */
    BRUSH_STATE_STOPPING, /**< 减速停止中 */
    BRUSH_STATE_FAULT,    /**< 故障，需调用 hal_motor_recover() 恢复对应电机 */
} brush_state_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化刷子模块。
 *
 * @param exec       共享 motor_executor_t，须已完成 motor_init。
 * @param motor_side 侧刷对应的电机索引（由机型层分配）。
 * @param motor_top  顶刷对应的电机索引（由机型层分配）。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 */
sw_err_t brush_init(hal_motor_exec_t *exec, int motor_side, int motor_top);

/**
 * @brief 命令指定刷子运行（异步）。
 *
 * 若目标刷子当前未运行，将先停止另一路（若在运行），再下发启动命令；
 * MCC 自动处理冷却排队与接触器切换时序。若目标刷子已在运行，则仅调速。
 *
 * @param id        目标刷子标识。
 * @param speed_gear 速度挡位，对应 motor_config_t 中的 gear_freq 索引。
 * @return SW_OK        命令已受理。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_PARAM    参数非法。
 *         SW_ERR_STATE    当前处于故障态，须先恢复。
 */
sw_err_t brush_start(brush_id_t id, int speed_gear);

/**
 * @brief 命令停止当前刷子（异步）。
 *
 * @return SW_OK        命令已受理。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_STATE    当前处于故障态。
 */
sw_err_t brush_stop(void);

/**
 * @brief 查询刷子模块当前状态。
 */
brush_state_t brush_state(void);

/**
 * @brief 查询最近一次 brush_start() 请求的刷子标识。
 */
brush_id_t brush_selected(void);

/**
 * @brief 查询底层电机故障码（仅在 BRUSH_STATE_FAULT 时有实质意义）。
 */
hal_motor_fault_code_t brush_fault_code(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_BRUSH_H */
