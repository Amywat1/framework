/**
 * @file    m8_cloud_model.c
 * @brief   M8 云端物模型点位表组装
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_model.h"
#include "projects/m8/adapters/cloud/m8_cloud_points.h"

#define M8_CLOUD_POINT_ENTRY(sem, access, policy, id, type, get_fn, set_fn, cmd, service_fn) \
    {                                                                                          \
        { (id), (type), (get_fn), (set_fn) },                                                  \
        (access),                                                                              \
        (sem),                                                                                 \
        (policy),                                                                              \
        (cmd),                                                                                 \
        (service_fn),                                                                          \
    },

static const cloud_point_entry_t s_m8_cloud_model[] = {
    M8_CLOUD_POINTS(M8_CLOUD_POINT_ENTRY)
};

const cloud_point_entry_t *m8_cloud_model(size_t *out_count)
{
    if (out_count != NULL)
    {
        *out_count = sizeof(s_m8_cloud_model) / sizeof(s_m8_cloud_model[0]);
    }
    return s_m8_cloud_model;
}
