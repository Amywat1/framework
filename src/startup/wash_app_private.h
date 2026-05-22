#ifndef SRC_STARTUP_WASH_APP_PRIVATE_H
#define SRC_STARTUP_WASH_APP_PRIVATE_H

#include "startup/wash_app.h"

operation_result_t wash_app_private_read_device_runtime(const wash_app_t *runtime,
                                                                  device_runtime_t *out_device_runtime);
controller_scheduler_t *wash_app_private_scheduler(const wash_app_t *runtime);

#endif
