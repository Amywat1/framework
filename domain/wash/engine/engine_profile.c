/**
 * @file    engine_profile.c
 * @brief   洗车引擎车辆轮廓查询 provider 契约实现
 */

#include "domain/wash/engine/engine_profile.h"

#include <stddef.h>

static const engine_profile_provider_t *s_provider;

void engine_profile_register(const engine_profile_provider_t *provider)
{
    s_provider = provider;
}

const engine_profile_provider_t *engine_profile_get_provider(void)
{
    return s_provider;
}

double engine_profile_height_at(double pos, double default_value)
{
    double                            out = default_value;
    const engine_profile_provider_t *p   = engine_profile_get_provider();

    if ((p == NULL) || (p->height_at == NULL)) {
        return default_value;
    }
    if (!p->height_at(p->ctx, pos, default_value, &out)) {
        return default_value;
    }
    return out;
}

bool engine_profile_in_zone(const char *zone, double pos, bool default_value)
{
    bool                              out = default_value;
    const engine_profile_provider_t *p   = engine_profile_get_provider();

    if ((zone == NULL) || (p == NULL) || (p->in_zone == NULL)) {
        return default_value;
    }
    if (!p->in_zone(p->ctx, zone, pos, default_value, &out)) {
        return default_value;
    }
    return out;
}
