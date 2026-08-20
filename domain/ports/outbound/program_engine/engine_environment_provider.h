/**
 * @file engine_environment_provider.h
 * @brief 方案引擎运行环境 provider SPI。
 * @note 仅 provider 实现包含本文件；domain 消费者只包含 port 头文件。
 */
#ifndef DOMAIN_PORTS_OUTBOUND_PROGRAM_ENGINE_ENGINE_ENVIRONMENT_PROVIDER_H
#define DOMAIN_PORTS_OUTBOUND_PROGRAM_ENGINE_ENGINE_ENVIRONMENT_PROVIDER_H

#include "domain/ports/outbound/program_engine/engine_environment_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief IO provider 操作集；signal/axis ID 来自绑定目录。 */
typedef struct {
    sw_err_t (*begin_tick)(void *ctx);
    sw_err_t (*read_signal)(void *ctx, unsigned signal_id, int *out_value);
    sw_err_t (*read_axis)(void *ctx, unsigned axis_id, engine_axis_sample_t *out_sample);
    void (*end_tick)(void *ctx);
} engine_io_ops_t;

/** @brief 执行机构 provider 操作集；resource ID 来自绑定目录。 */
typedef struct {
    sw_err_t (*apply)(void *ctx, unsigned resource_id, const engine_intent_t *intent);
    sw_err_t (*release)(void *ctx, unsigned resource_id);
    sw_err_t (*halt_all)(void *ctx);
    sw_err_t (*is_settled)(void *ctx, unsigned resource_id, bool *out_settled);
} engine_actuator_ops_t;

/** @brief 车辆轮廓 provider 操作集。 */
typedef struct {
    sw_err_t (*begin_tick)(void *ctx);
    sw_err_t (*height_at)(void *ctx, double position, double default_value, double *out_height);
    sw_err_t (*in_zone)(void *ctx, const char *zone, double position, bool default_value, bool *out_in_zone);
    void (*end_tick)(void *ctx);
} engine_profile_ops_t;

/** @brief 项目变量 provider 操作集；变量 ID 来自绑定目录。 */
typedef struct {
    sw_err_t (*begin_tick)(void *ctx);
    sw_err_t (*read)(void *ctx, unsigned variable_id, double *out_value);
    void (*end_tick)(void *ctx);
} engine_variable_ops_t;

struct engine_io {
    const engine_io_ops_t     *ops;
    void                      *ctx;
    const engine_io_catalog_t *catalog;
};

struct engine_actuator {
    const engine_actuator_ops_t     *ops;
    void                            *ctx;
    const engine_actuator_catalog_t *catalog;
};

struct engine_profile {
    const engine_profile_ops_t *ops;
    void                       *ctx;
};

struct engine_variable {
    const engine_variable_ops_t     *ops;
    void                            *ctx;
    const engine_variable_catalog_t *catalog;
};

/** @brief 绑定 provider 拥有的 IO 句柄存储。 */
sw_err_t engine_io_provider_bind(engine_io_t               *io,
                                 const engine_io_ops_t     *ops,
                                 void                      *ctx,
                                 const engine_io_catalog_t *catalog);

/** @brief 绑定 provider 拥有的执行机构句柄存储。 */
sw_err_t engine_actuator_provider_bind(engine_actuator_t               *actuator,
                                       const engine_actuator_ops_t     *ops,
                                       void                            *ctx,
                                       const engine_actuator_catalog_t *catalog);

/** @brief 绑定 provider 拥有的轮廓句柄存储。 */
sw_err_t engine_profile_provider_bind(engine_profile_t *profile, const engine_profile_ops_t *ops, void *ctx);

/** @brief 绑定 provider 拥有的项目变量句柄存储。 */
sw_err_t engine_variable_provider_bind(engine_variable_t               *variable,
                                       const engine_variable_ops_t     *ops,
                                       void                            *ctx,
                                       const engine_variable_catalog_t *catalog);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_PROGRAM_ENGINE_ENGINE_ENVIRONMENT_PROVIDER_H */
