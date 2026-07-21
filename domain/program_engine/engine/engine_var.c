/**
 * @file    engine_var.c
 * @brief   引擎项目变量提供者注册
 * @author  HUWANGWEI
 * @date    2026-07-21
 */

#include "domain/program_engine/engine/engine_var.h"

#include <stddef.h>
#include <string.h>

static const engine_var_provider_t *s_provider = NULL;

void engine_var_register(const engine_var_provider_t *provider)
{
    s_provider = provider;
}

const engine_var_provider_t *engine_var_get(void)
{
    return s_provider;
}

bool engine_var_name_known(const char *name)
{
    unsigned i;

    if ((name == NULL) || (s_provider == NULL) || (s_provider->names == NULL)) {
        return false;
    }
    for (i = 0U; i < s_provider->name_count; ++i) {
        if ((s_provider->names[i] != NULL) && (strcmp(name, s_provider->names[i]) == 0)) {
            return true;
        }
    }
    return false;
}
