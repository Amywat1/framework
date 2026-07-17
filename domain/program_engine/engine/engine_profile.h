/**
 * @file    engine_profile.h
 * @brief   方案引擎可选车辆轮廓查询 provider 契约
 */

#ifndef DOMAIN_PROGRAM_ENGINE_PROFILE_H
#define DOMAIN_PROGRAM_ENGINE_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef struct {
    bool (*height_at)(void *ctx, double pos, double default_value, double *out_height);
    bool (*in_zone)(void *ctx, const char *zone, double pos, bool default_value, bool *out_in_zone);
    void *ctx;
} engine_profile_provider_t;

void                             engine_profile_register(const engine_profile_provider_t *provider);
const engine_profile_provider_t *engine_profile_get_provider(void);

double engine_profile_height_at(double pos, double default_value);
bool   engine_profile_in_zone(const char *zone, double pos, bool default_value);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROGRAM_ENGINE_PROFILE_H */
