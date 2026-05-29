#ifndef DOMAIN_PORTS_SENSOR_PORT_H
#define DOMAIN_PORTS_SENSOR_PORT_H

#include <stdbool.h>

#include "domain/model/position_trigger.h"

/**
 * @file sensor_port.h
 * @brief 定义平台层运行时事实读取端口。
 */

/**
 * @brief 启动前预检传感器快照，用于判断机器是否具备开始洗车的条件。
 */
typedef struct sensor_snapshot_t
{
    bool safety_ok;       /**< 安全互锁状态正常 */
    bool estop_active;    /**< 急停信号激活 */
    bool vehicle_present; /**< 检测到车辆 */
    bool vehicle_allowed; /**< 车型在当前程序允许范围内 */
    bool resource_ok;     /**< 水、电、气等资源就绪 */
    bool position_ok;     /**< 龙门位置处于安全起始区域 */
} sensor_snapshot_t;

typedef struct actuator_feedback_snapshot_t
{
    bool roof_brush_follow_ok;
    bool roof_brush_stopped_feedback_available;
    bool roof_brush_stopped;
    bool side_brush_stopped_feedback_available;
    bool side_brush_stopped;
    bool chemical_closed_feedback_available;
    bool chemical_closed;
    bool ro_water_closed_feedback_available;
    bool ro_water_closed;
    bool dryer_closed_feedback_available;
    bool dryer_closed;
    bool roof_brush_home_feedback_available;
    bool roof_brush_home_reached;
} actuator_feedback_snapshot_t;

typedef struct runtime_snapshot_t
{
    position_snapshot_t position_snapshot;
    actuator_feedback_snapshot_t actuator_feedback;
} runtime_snapshot_t;

typedef struct sensor_port_t
{
    void *context;
    /** @brief 读取运行时执行快照（位置 + 执行机构反馈）；实现必须支持并发读取并返回一致快照，成功返回 0，失败返回非零。 */
    int (*read_machine_snapshot)(void *context, runtime_snapshot_t *runtime_snapshot);
    /** @brief 读取启动前预检快照；实现必须保证并发读取时字段来自同一观测视图，成功返回 0，失败返回非零。 */
    int (*read_snapshot)(void *context, sensor_snapshot_t *sensor_snapshot);
} sensor_port_t;

#endif
