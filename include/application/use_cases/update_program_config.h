#ifndef APPLICATION_USE_CASES_UPDATE_PROGRAM_CONFIG_H
#define APPLICATION_USE_CASES_UPDATE_PROGRAM_CONFIG_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

operation_result_t update_program_config_execute(system_context_t *system_context, const char *config_path);

#endif

