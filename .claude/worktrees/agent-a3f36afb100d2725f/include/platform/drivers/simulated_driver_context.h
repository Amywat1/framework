#ifndef PLATFORM_DRIVERS_SIMULATED_DRIVER_CONTEXT_H
#define PLATFORM_DRIVERS_SIMULATED_DRIVER_CONTEXT_H

#include <stdbool.h>
#include <pthread.h>

#include "domain/ports/sensor_port.h"

/**
 * @file simulated_driver_context.h
 * @brief 定义仿真驱动共享上下文。
 */
typedef struct simulated_driver_context_t
{
    pthread_mutex_t mutex;
    runtime_snapshot_t runtime_snapshot;
    /* precheck_service 预检字段：默认全通过，置反可覆盖触发对应拒绝路径 */
    bool precheck_read_should_fail;    /**< 置 true 使 read_snapshot 返回 -1（独立于 read_runtime_snapshot）*/
    bool precheck_safety_should_fail;  /**< 置 true 可触发安全互锁拒绝 */
    bool precheck_estop_active;        /**< 置 true 可触发急停拒绝 */
    bool precheck_vehicle_absent;      /**< 置 true 可触发车辆未检测到拒绝 */
    bool precheck_vehicle_not_allowed; /**< 置 true 可触发车型不允许拒绝 */
    bool precheck_resource_fail;       /**< 置 true 可触发资源未就绪拒绝 */
    bool precheck_position_fail;       /**< 置 true 可触发位置检查失败拒绝 */
    bool roof_brush_follow_enabled;
    bool side_brush_enabled;
    bool chemical_enabled;
    bool ro_water_enabled;
    bool dryer_enabled;
    bool roof_follow_feedback_available;
    bool roof_stop_feedback_available;
    bool side_stop_feedback_available;
    bool chemical_close_feedback_available;
    bool ro_water_close_feedback_available;
    bool dryer_close_feedback_available;
    bool roof_home_feedback_available;
    bool roof_home_reached;
    bool runtime_snapshot_read_should_fail;
    bool stop_all_should_fail;
    bool roof_home_command_should_fail;
    bool chemical_set_command_should_fail;
    bool chemical_stop_command_should_fail;
    bool roof_stop_command_should_fail;
    bool side_stop_command_should_fail;
    bool ro_water_close_command_should_fail;
    bool dryer_close_command_should_fail;
    int motion_command_count;
    int roof_follow_command_count;
    int side_brush_command_count;
    int chemical_command_count;
    int ro_water_command_count;
    int dryer_command_count;
    int roof_stop_command_count;
    int side_stop_command_count;
    int chemical_stop_command_count;
    int ro_water_close_command_count;
    int dryer_close_command_count;
    int roof_home_command_count;
    int stop_all_command_count;
} simulated_driver_context_t;

/**
 * @brief 初始化仿真驱动共享上下文。
 * @param driver_context 共享上下文，不能为空。
 */
void simulated_driver_context_init(simulated_driver_context_t *driver_context);

/**
 * @brief 锁住仿真驱动共享上下文。
 * @param driver_context 共享上下文，不能为空。
 */
void simulated_driver_context_lock(simulated_driver_context_t *driver_context);

/**
 * @brief 解锁仿真驱动共享上下文。
 * @param driver_context 共享上下文，不能为空。
 */
void simulated_driver_context_unlock(simulated_driver_context_t *driver_context);

#endif
