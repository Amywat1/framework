#ifndef TESTS_STUBS_SNACK_CLOUD_CLOUD_MODEL_FAKE_H
#define TESTS_STUBS_SNACK_CLOUD_CLOUD_MODEL_FAKE_H

#include "common/sw_error.h"

void snack_cloud_model_fake_reset(void);
void snack_cloud_model_fake_set_full_result(sw_err_t result);
void snack_cloud_model_fake_set_delta_result(sw_err_t result);

#endif /* TESTS_STUBS_SNACK_CLOUD_CLOUD_MODEL_FAKE_H */
