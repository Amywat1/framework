#ifndef SRC_APPLICATION_COORDINATORS_CONTROLLER_RUNTIME_PRIVATE_H
#define SRC_APPLICATION_COORDINATORS_CONTROLLER_RUNTIME_PRIVATE_H

#include "application/coordinators/controller_runtime.h"

operation_result_t controller_runtime_private_read_system_context(const controller_runtime_t *runtime,
                                                                  system_context_t *system_context);
controller_scheduler_t *controller_runtime_private_scheduler(const controller_runtime_t *runtime);

#endif
