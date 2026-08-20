/**
 * @file engine_environment_port.c
 * @brief 方案引擎运行环境不透明句柄分派。
 */
#include "domain/ports/outbound/program_engine/engine_environment_provider.h"

#include <stddef.h>
#include <string.h>

static bool names_valid(const char *const *names, unsigned count)
{
    if ((count > 0U) && (names == NULL)) {
        return false;
    }
    for (unsigned i = 0U; i < count; ++i) {
        if ((names[i] == NULL) || (names[i][0] == '\0')) {
            return false;
        }
    }
    return true;
}

static sw_err_t find_name(const char *const *names, unsigned count, const char *name, unsigned *out_id)
{
    if ((name == NULL) || (out_id == NULL)) {
        return SW_ERR_PARAM;
    }
    for (unsigned i = 0U; i < count; ++i) {
        if (strcmp(names[i], name) == 0) {
            *out_id = i;
            return SW_OK;
        }
    }
    return SW_ERR_NOT_FOUND;
}

static bool io_valid(const engine_io_t *io)
{
    return (io != NULL) && (io->ops != NULL) && (io->ctx != NULL) && (io->catalog != NULL)
           && (io->ops->read_signal != NULL) && (io->ops->read_axis != NULL)
           && names_valid(io->catalog->signals, io->catalog->signal_count)
           && names_valid(io->catalog->axes, io->catalog->axis_count);
}

static bool actuator_valid(const engine_actuator_t *actuator)
{
    return (actuator != NULL) && (actuator->ops != NULL) && (actuator->ctx != NULL) && (actuator->catalog != NULL)
           && (actuator->ops->apply != NULL) && (actuator->ops->release != NULL) && (actuator->ops->halt_all != NULL)
           && names_valid(actuator->catalog->resources, actuator->catalog->resource_count)
           && names_valid(actuator->catalog->water_paths, actuator->catalog->water_path_count);
}

sw_err_t engine_environment_validate(const engine_environment_t *environment)
{
    if (environment == NULL) {
        return SW_ERR_PARAM;
    }
    return (io_valid(environment->io) && actuator_valid(environment->actuator)) ? SW_OK : SW_ERR_NOT_INIT;
}

sw_err_t engine_io_provider_bind(engine_io_t               *io,
                                 const engine_io_ops_t     *ops,
                                 void                      *ctx,
                                 const engine_io_catalog_t *catalog)
{
    if ((io == NULL) || (ops == NULL) || (ctx == NULL) || (catalog == NULL) || (ops->read_signal == NULL)
        || (ops->read_axis == NULL) || !names_valid(catalog->signals, catalog->signal_count)
        || !names_valid(catalog->axes, catalog->axis_count)) {
        return SW_ERR_PARAM;
    }
    io->ops     = ops;
    io->ctx     = ctx;
    io->catalog = catalog;
    return SW_OK;
}

const engine_io_catalog_t *engine_io_catalog(const engine_io_t *io)
{
    return io_valid(io) ? io->catalog : NULL;
}

sw_err_t engine_io_find_signal(const engine_io_t *io, const char *name, unsigned *out_id)
{
    return io_valid(io) ? find_name(io->catalog->signals, io->catalog->signal_count, name, out_id) : SW_ERR_NOT_INIT;
}

sw_err_t engine_io_find_axis(const engine_io_t *io, const char *name, unsigned *out_id)
{
    return io_valid(io) ? find_name(io->catalog->axes, io->catalog->axis_count, name, out_id) : SW_ERR_NOT_INIT;
}

sw_err_t engine_io_begin_tick(engine_io_t *io)
{
    if (!io_valid(io)) {
        return SW_ERR_NOT_INIT;
    }
    return (io->ops->begin_tick != NULL) ? io->ops->begin_tick(io->ctx) : SW_OK;
}

sw_err_t engine_io_read_signal(engine_io_t *io, unsigned signal_id, int *out_value)
{
    if ((out_value == NULL) || !io_valid(io)) {
        return (out_value == NULL) ? SW_ERR_PARAM : SW_ERR_NOT_INIT;
    }
    if (signal_id >= io->catalog->signal_count) {
        return SW_ERR_PARAM;
    }
    return io->ops->read_signal(io->ctx, signal_id, out_value);
}

sw_err_t engine_io_read_axis(engine_io_t *io, unsigned axis_id, engine_axis_sample_t *out_sample)
{
    if ((out_sample == NULL) || !io_valid(io)) {
        return (out_sample == NULL) ? SW_ERR_PARAM : SW_ERR_NOT_INIT;
    }
    if (axis_id >= io->catalog->axis_count) {
        return SW_ERR_PARAM;
    }
    return io->ops->read_axis(io->ctx, axis_id, out_sample);
}

void engine_io_end_tick(engine_io_t *io)
{
    if (io_valid(io) && (io->ops->end_tick != NULL)) {
        io->ops->end_tick(io->ctx);
    }
}

sw_err_t engine_actuator_provider_bind(engine_actuator_t               *actuator,
                                       const engine_actuator_ops_t     *ops,
                                       void                            *ctx,
                                       const engine_actuator_catalog_t *catalog)
{
    if ((actuator == NULL) || (ops == NULL) || (ctx == NULL) || (catalog == NULL) || (ops->apply == NULL)
        || (ops->release == NULL) || (ops->halt_all == NULL)
        || !names_valid(catalog->resources, catalog->resource_count)
        || !names_valid(catalog->water_paths, catalog->water_path_count)) {
        return SW_ERR_PARAM;
    }
    actuator->ops     = ops;
    actuator->ctx     = ctx;
    actuator->catalog = catalog;
    return SW_OK;
}

const engine_actuator_catalog_t *engine_actuator_catalog(const engine_actuator_t *actuator)
{
    return actuator_valid(actuator) ? actuator->catalog : NULL;
}

sw_err_t engine_actuator_apply(engine_actuator_t *actuator, unsigned resource_id, const engine_intent_t *intent)
{
    if ((intent == NULL) || !actuator_valid(actuator)) {
        return (intent == NULL) ? SW_ERR_PARAM : SW_ERR_NOT_INIT;
    }
    if (resource_id >= actuator->catalog->resource_count) {
        return SW_ERR_PARAM;
    }
    return actuator->ops->apply(actuator->ctx, resource_id, intent);
}

sw_err_t engine_actuator_release(engine_actuator_t *actuator, unsigned resource_id)
{
    if (!actuator_valid(actuator)) {
        return SW_ERR_NOT_INIT;
    }
    if (resource_id >= actuator->catalog->resource_count) {
        return SW_ERR_PARAM;
    }
    return actuator->ops->release(actuator->ctx, resource_id);
}

sw_err_t engine_actuator_halt_all(engine_actuator_t *actuator)
{
    return actuator_valid(actuator) ? actuator->ops->halt_all(actuator->ctx) : SW_ERR_NOT_INIT;
}

sw_err_t engine_actuator_is_settled(engine_actuator_t *actuator, unsigned resource_id, bool *out_settled)
{
    if ((out_settled == NULL) || !actuator_valid(actuator)) {
        return (out_settled == NULL) ? SW_ERR_PARAM : SW_ERR_NOT_INIT;
    }
    if (resource_id >= actuator->catalog->resource_count) {
        return SW_ERR_PARAM;
    }
    if (actuator->ops->is_settled == NULL) {
        *out_settled = true;
        return SW_OK;
    }
    return actuator->ops->is_settled(actuator->ctx, resource_id, out_settled);
}

sw_err_t engine_profile_provider_bind(engine_profile_t *profile, const engine_profile_ops_t *ops, void *ctx)
{
    if ((profile == NULL) || (ops == NULL) || (ctx == NULL) || (ops->height_at == NULL) || (ops->in_zone == NULL)) {
        return SW_ERR_PARAM;
    }
    profile->ops = ops;
    profile->ctx = ctx;
    return SW_OK;
}

sw_err_t engine_profile_begin_tick(engine_profile_t *profile)
{
    if (profile == NULL) {
        return SW_OK;
    }
    if ((profile->ops == NULL) || (profile->ctx == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return (profile->ops->begin_tick != NULL) ? profile->ops->begin_tick(profile->ctx) : SW_OK;
}

sw_err_t engine_profile_height_at(engine_profile_t *profile, double position, double default_value, double *out_height)
{
    if (out_height == NULL) {
        return SW_ERR_PARAM;
    }
    if (profile == NULL) {
        *out_height = default_value;
        return SW_OK;
    }
    if ((profile->ops == NULL) || (profile->ctx == NULL) || (profile->ops->height_at == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return profile->ops->height_at(profile->ctx, position, default_value, out_height);
}

sw_err_t engine_profile_in_zone(engine_profile_t *profile,
                                const char       *zone,
                                double            position,
                                bool              default_value,
                                bool             *out_in_zone)
{
    if ((zone == NULL) || (out_in_zone == NULL)) {
        return SW_ERR_PARAM;
    }
    if (profile == NULL) {
        *out_in_zone = default_value;
        return SW_OK;
    }
    if ((profile->ops == NULL) || (profile->ctx == NULL) || (profile->ops->in_zone == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return profile->ops->in_zone(profile->ctx, zone, position, default_value, out_in_zone);
}

void engine_profile_end_tick(engine_profile_t *profile)
{
    if ((profile != NULL) && (profile->ops != NULL) && (profile->ctx != NULL) && (profile->ops->end_tick != NULL)) {
        profile->ops->end_tick(profile->ctx);
    }
}

sw_err_t engine_variable_provider_bind(engine_variable_t               *variable,
                                       const engine_variable_ops_t     *ops,
                                       void                            *ctx,
                                       const engine_variable_catalog_t *catalog)
{
    if ((variable == NULL) || (ops == NULL) || (ctx == NULL) || (catalog == NULL) || (ops->read == NULL)
        || !names_valid(catalog->names, catalog->name_count)) {
        return SW_ERR_PARAM;
    }
    variable->ops     = ops;
    variable->ctx     = ctx;
    variable->catalog = catalog;
    return SW_OK;
}

const engine_variable_catalog_t *engine_variable_catalog(const engine_variable_t *variable)
{
    if ((variable == NULL) || (variable->ops == NULL) || (variable->ctx == NULL) || (variable->catalog == NULL)
        || (variable->ops->read == NULL)) {
        return NULL;
    }
    return variable->catalog;
}

sw_err_t engine_variable_find(const engine_variable_t *variable, const char *name, unsigned *out_id)
{
    const engine_variable_catalog_t *catalog = engine_variable_catalog(variable);

    return (catalog != NULL) ? find_name(catalog->names, catalog->name_count, name, out_id) : SW_ERR_NOT_INIT;
}

sw_err_t engine_variable_begin_tick(engine_variable_t *variable)
{
    if (variable == NULL) {
        return SW_OK;
    }
    if (engine_variable_catalog(variable) == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return (variable->ops->begin_tick != NULL) ? variable->ops->begin_tick(variable->ctx) : SW_OK;
}

sw_err_t engine_variable_read(engine_variable_t *variable, unsigned variable_id, double *out_value)
{
    const engine_variable_catalog_t *catalog = engine_variable_catalog(variable);

    if (out_value == NULL) {
        return SW_ERR_PARAM;
    }
    if (catalog == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (variable_id >= catalog->name_count) {
        return SW_ERR_PARAM;
    }
    return variable->ops->read(variable->ctx, variable_id, out_value);
}

void engine_variable_end_tick(engine_variable_t *variable)
{
    if ((engine_variable_catalog(variable) != NULL) && (variable->ops->end_tick != NULL)) {
        variable->ops->end_tick(variable->ctx);
    }
}
