/**
 * @file engine_environment_port.h
 * @brief 方案引擎每实例运行环境出站端口。
 *
 * 引擎实例只持有本文件声明的不透明能力句柄。具体 provider、存储布局与硬件
 * 映射由 adapters 和项目 composition root 决定，运行期不得替换已注入能力。
 */
#ifndef DOMAIN_PORTS_OUTBOUND_PROGRAM_ENGINE_ENGINE_ENVIRONMENT_PORT_H
#define DOMAIN_PORTS_OUTBOUND_PROGRAM_ENGINE_ENGINE_ENVIRONMENT_PORT_H

#include "common/sw_error.h"
#include "domain/program_engine/model/engine_model.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 引擎 IO 名称目录；数组下标同时是 provider 稳定 ID。 */
typedef struct {
    const char *const *signals;
    unsigned           signal_count;
    const char *const *axes;
    unsigned           axis_count;
} engine_io_catalog_t;

/** @brief 执行机构名称目录；数组下标同时是 provider 稳定 ID。 */
typedef struct {
    const char *const *resources;
    unsigned           resource_count;
    const char *const *water_paths;
    unsigned           water_path_count;
} engine_actuator_catalog_t;

/** @brief 项目变量名称目录；数组下标同时是 provider 稳定 ID。 */
typedef struct {
    const char *const *names;
    unsigned           name_count;
} engine_variable_catalog_t;

/** @brief IO 轴的一拍快照。 */
typedef struct {
    double position;
    double speed;
    bool   valid;
} engine_axis_sample_t;

/** @brief IO provider 的不透明句柄。 */
typedef struct engine_io engine_io_t;
/** @brief 执行机构 provider 的不透明句柄。 */
typedef struct engine_actuator engine_actuator_t;
/** @brief 车辆轮廓 provider 的不透明句柄。 */
typedef struct engine_profile engine_profile_t;
/** @brief 项目变量 provider 的不透明句柄。 */
typedef struct engine_variable engine_variable_t;

/**
 * @brief 引擎运行环境；由 composition root 组装并在 engine_create 时按值锁存。
 * @note io 与 actuator 必填；profile、variable、pre_tick 可为空。
 */
typedef struct {
    engine_io_t       *io;
    engine_actuator_t *actuator;
    engine_profile_t  *profile;
    engine_variable_t *variable;
    sw_err_t (*pre_tick)(void *ctx);
    void *pre_tick_ctx;
} engine_environment_t;

/** @brief 校验运行环境必填能力是否完整。 */
sw_err_t engine_environment_validate(const engine_environment_t *environment);

/** @brief 获取 IO 目录；句柄无效时返回 NULL。 */
const engine_io_catalog_t *engine_io_catalog(const engine_io_t *io);

/** @brief 按名称绑定信号稳定 ID。 */
sw_err_t engine_io_find_signal(const engine_io_t *io, const char *name, unsigned *out_id);

/** @brief 按名称绑定轴稳定 ID。 */
sw_err_t engine_io_find_axis(const engine_io_t *io, const char *name, unsigned *out_id);

/** @brief 开始一拍采样；provider 可在此锁存硬件输入。 */
sw_err_t engine_io_begin_tick(engine_io_t *io);

/** @brief 按稳定 ID 读取信号。 */
sw_err_t engine_io_read_signal(engine_io_t *io, unsigned signal_id, int *out_value);

/** @brief 按稳定 ID 读取轴。 */
sw_err_t engine_io_read_axis(engine_io_t *io, unsigned axis_id, engine_axis_sample_t *out_sample);

/** @brief 结束一拍采样。 */
void engine_io_end_tick(engine_io_t *io);

/** @brief 获取执行机构目录；句柄无效时返回 NULL。 */
const engine_actuator_catalog_t *engine_actuator_catalog(const engine_actuator_t *actuator);

/** @brief 提交机构意图；resource_id 必须来自同一句柄的目录。 */
sw_err_t engine_actuator_apply(engine_actuator_t *actuator, unsigned resource_id, const engine_intent_t *intent);

/** @brief 释放机构资源。 */
sw_err_t engine_actuator_release(engine_actuator_t *actuator, unsigned resource_id);

/** @brief 执行机构全停。 */
sw_err_t engine_actuator_halt_all(engine_actuator_t *actuator);

/**
 * @brief 查询执行机构资源是否已结算。
 * @param actuator 执行机构 provider 句柄。
 * @param resource_id 来自同一句柄目录的资源 ID。
 * @param out_settled 返回是否已结算。
 * @return SW_OK 查询成功；SW_ERR_PARAM 参数无效；SW_ERR_NOT_INIT 句柄无效。
 * @note provider 未实现结算查询时返回已结算，用于无需运动结算的后端。
 */
sw_err_t engine_actuator_is_settled(engine_actuator_t *actuator, unsigned resource_id, bool *out_settled);

/** @brief 开始一拍轮廓查询；provider 可在此锁存轮廓版本。 */
sw_err_t engine_profile_begin_tick(engine_profile_t *profile);

/** @brief 查询指定位置高度；未注入 profile 时返回 default_value。 */
sw_err_t engine_profile_height_at(engine_profile_t *profile, double position, double default_value, double *out_height);

/** @brief 查询指定位置是否位于区域；未注入 profile 时返回 default_value。 */
sw_err_t engine_profile_in_zone(engine_profile_t *profile,
                                const char       *zone,
                                double            position,
                                bool              default_value,
                                bool             *out_in_zone);

/** @brief 结束一拍轮廓查询。 */
void engine_profile_end_tick(engine_profile_t *profile);

/** @brief 获取项目变量目录；句柄无效时返回 NULL。 */
const engine_variable_catalog_t *engine_variable_catalog(const engine_variable_t *variable);

/** @brief 按名称绑定项目变量稳定 ID。 */
sw_err_t engine_variable_find(const engine_variable_t *variable, const char *name, unsigned *out_id);

/** @brief 开始一拍变量查询；provider 可在此锁存数据版本。 */
sw_err_t engine_variable_begin_tick(engine_variable_t *variable);

/** @brief 按稳定 ID 读取项目变量。 */
sw_err_t engine_variable_read(engine_variable_t *variable, unsigned variable_id, double *out_value);

/** @brief 结束一拍变量查询。 */
void engine_variable_end_tick(engine_variable_t *variable);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_PROGRAM_ENGINE_ENGINE_ENVIRONMENT_PORT_H */
