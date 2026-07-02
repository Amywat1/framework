/**
 * @file    brush.h
 * @brief   刷子机构领域层接口。
 *
 * 管理侧刷与顶刷共享一台变频器的控制逻辑：两刷通过双线圈接触器切换，
 * 本模块负责"停止变频器 → 断电旧接触器 → 通电新接触器 → 重启变频器"的
 * 完整时序，对外仅暴露 brush_start / brush_stop / brush_tick 三个核心接口。
 *
 * 调用方须以固定节拍调用 brush_tick()（推荐与电机 tick 节拍一致）。
 * 本模块内部持有并驱动 motor_executor_t，调用方无需另外调用 motor_tick()。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_BRUSH_H
#define DOMAIN_DEVICE_MECHANISM_BRUSH_H

#include "motor/motor_executor.h"
#include "common/sw_error.h"
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
 * @brief 刷子模块整体状态。
 *
 * 状态转移：
 *   IDLE ──(brush_start)──► CONTACTOR_OFF ──► CONTACTOR_ON ──► STARTING ──► RUNNING
 *   IDLE ──(同刷子start)──► STARTING ──► RUNNING
 *   RUNNING ──(brush_stop)──► STOPPING ──► IDLE
 *   RUNNING ──(切换brush_start)──► STOPPING ──► CONTACTOR_OFF ──► CONTACTOR_ON ──► STARTING ──► RUNNING
 *   任意态 ──(电机故障/急停)──► FAULT
 */
typedef enum {
    BRUSH_STATE_IDLE = 0,      /**< 空闲：变频器停止，接触器保持当前位置 */
    BRUSH_STATE_STOPPING,      /**< 减速停止中（为接触器切换或最终停止） */
    BRUSH_STATE_CONTACTOR_OFF, /**< 等待旧接触器断电释放 */
    BRUSH_STATE_CONTACTOR_ON,  /**< 等待新接触器通电吸合稳定 */
    BRUSH_STATE_STARTING,      /**< 变频器启动中 */
    BRUSH_STATE_RUNNING,       /**< 运行中 */
    BRUSH_STATE_FAULT,         /**< 故障，需外部调用 motor_recover() 后恢复 */
} brush_state_t;

/**
 * @brief 接触器操作回调，由机型适配层实现并注入。
 *
 * 两个回调均为非阻塞调用，仅驱动 DO 输出；
 * 稳定等待时间由刷子模块状态机负责管理。
 */
typedef struct {
    /** @brief 通电指定刷子的接触器线圈，必填。 */
    sw_err_t (*set_on)(void *ctx, brush_id_t id);
    /** @brief 断电指定刷子的接触器线圈，必填。 */
    sw_err_t (*set_off)(void *ctx, brush_id_t id);
    /** @brief 透传给两个回调的上下文指针。 */
    void *ctx;
} brush_contactor_ops_t;

/**
 * @brief 接触器时序配置，由机型适配层注入。
 */
typedef struct {
    uint32_t release_ms; /**< 断电后等待接触器完全释放的时间（ms） */
    uint32_t close_ms;   /**< 通电后等待接触器吸合并稳定的时间（ms） */
} brush_contactor_cfg_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化刷子模块。
 *
 * @param exec          已完成 motor_init 的执行器，刷子模块独占 motor 0。
 * @param contactor_ops 接触器操作回调，set_on 和 set_off 均不得为 NULL。
 * @param contactor_cfg 接触器时序配置，不得为 NULL。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 *
 * @note 调用前须确保 m8_vfd_setup() 已完成，且两路接触器 DO 均已置低。
 *       初始化后接触器处于未吸合状态，首次 brush_start() 将按需通电接触器。
 */
sw_err_t brush_init(motor_executor_t           *exec,
                    const brush_contactor_ops_t *contactor_ops,
                    const brush_contactor_cfg_t *contactor_cfg);

/**
 * @brief 请求启动指定刷子（异步）。
 *
 * 若目标刷子与当前接触器选中的刷子不同，模块将自动执行：
 *   停止变频器 → 断电旧接触器 → 通电新接触器 → 重启变频器。
 * 实际过程由 brush_tick() 推进，函数本身仅记录请求并返回。
 *
 * @param id        目标刷子标识。
 * @param speed_gear 速度挡位，对应 motor_config_t 中的 gear_freq 索引。
 * @return SW_OK        请求已受理。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_PARAM    参数非法。
 *         SW_ERR_STATE    当前处于故障态，须先恢复。
 */
sw_err_t brush_start(brush_id_t id, int speed_gear);

/**
 * @brief 请求停止当前刷子（异步）。
 *
 * 发出停止请求，实际过程由 brush_tick() 推进。
 * 若当前已空闲，调用无副作用。
 * 接触器断电由下次 brush_start() 按需触发，停止后接触器保持当前位置。
 *
 * @return SW_OK        请求已受理。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_STATE    当前处于故障态。
 */
sw_err_t brush_stop(void);

/**
 * @brief 刷子模块周期处理，须以固定节拍调用（推荐 20ms）。
 *
 * 内部同时驱动 motor_tick() 和刷子状态机，调用方无需单独调用 motor_tick()。
 */
void brush_tick(void);

/**
 * @brief 查询刷子模块当前状态。
 */
brush_state_t brush_state(void);

/**
 * @brief 查询当前接触器选中的刷子（仅当接触器已吸合时有意义）。
 */
brush_id_t brush_selected(void);

/**
 * @brief 查询接触器当前是否已吸合。
 */
bool brush_contactor_engaged(void);

/**
 * @brief 查询底层电机故障码（仅在 BRUSH_STATE_FAULT 时有实质意义）。
 */
motor_fault_code_t brush_fault_code(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_BRUSH_H */
