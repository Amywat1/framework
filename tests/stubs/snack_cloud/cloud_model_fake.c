#include "domain/cloud/cloud_model.h"

#include <stdio.h>
#include <string.h>

static sw_err_t s_full_result;
static sw_err_t s_delta_result;
static char     s_full_json[128];
static char     s_delta_json[128];

void snack_cloud_model_fake_reset(void)
{
    s_full_result  = SW_OK;
    s_delta_result = SW_OK;
    snprintf(s_full_json, sizeof(s_full_json), "{\"full\":true}");
    snprintf(s_delta_json, sizeof(s_delta_json), "{\"delta\":true}");
}

void snack_cloud_model_fake_set_full_result(sw_err_t result)
{
    s_full_result = result;
}

void snack_cloud_model_fake_set_delta_result(sw_err_t result)
{
    s_delta_result = result;
}

sw_err_t cloud_json_build_properties(char *buf, size_t buf_size)
{
    if (s_full_result != SW_OK) {
        return s_full_result;
    }
    if ((buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }
    snprintf(buf, buf_size, "%s", s_full_json);
    return SW_OK;
}

sw_err_t cloud_json_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size)
{
    (void)ids;
    (void)count;
    if (s_delta_result != SW_OK) {
        return s_delta_result;
    }
    if ((buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }
    snprintf(buf, buf_size, "%s", s_delta_json);
    return SW_OK;
}
