/**
 * @file    cloud_point_watcher.c
 * @brief   云端物模型 ON_CHANGE 点位变更检测实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/cloud/cloud_point_watcher.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include <string.h>

#define CLOUD_POINT_WATCHER_MAX   64U

typedef struct
{
    bool           valid;
    point_type_t   type;
    point_value_t  value;
} cloud_point_shadow_t;

static const cloud_point_entry_t *s_entries     = NULL;
static size_t                     s_entry_count = 0U;
static cloud_point_shadow_t       s_shadow[CLOUD_POINT_WATCHER_MAX];

static bool value_equal(point_type_t type,
                         const point_value_t *a,
                         const point_value_t *b)
{
    switch (type)
    {
        case POINT_TYPE_BOOL:
            return a->b == b->b;
        case POINT_TYPE_INT:
            return a->i == b->i;
        case POINT_TYPE_FLOAT:
            return a->f == b->f;
        case POINT_TYPE_STRING:
            return strcmp(a->s, b->s) == 0;
        default:
            return false;
    }
}

static void publish_dirty(size_t index)
{
    (void)event_publish(EVT_CLOUD_POINT_DIRTY, (uint32_t)index);
}

sw_err_t cloud_point_watcher_init(const cloud_point_entry_t *entries, size_t count)
{
    if ((entries == NULL) || (count == 0U) || (count > CLOUD_POINT_WATCHER_MAX))
    {
        return SW_ERR_PARAM;
    }

    memset(s_shadow, 0, sizeof(s_shadow));
    s_entries     = entries;
    s_entry_count = count;

    for (size_t i = 0U; i < count; i++)
    {
        point_value_t val;

        if ((entries[i].report_policy != CLOUD_REPORT_ON_CHANGE) ||
            (entries[i].base.get == NULL))
        {
            continue;
        }

        if (entries[i].base.get(&val) != SW_OK)
        {
            continue;
        }

        s_shadow[i].valid = true;
        s_shadow[i].type  = entries[i].base.type;
        s_shadow[i].value = val;
    }

    return SW_OK;
}

void cloud_point_watcher_poll(void)
{
    size_t i;

    if (s_entries == NULL)
    {
        return;
    }

    for (i = 0U; i < s_entry_count; i++)
    {
        const cloud_point_entry_t *entry = &s_entries[i];
        point_value_t              now;

        if ((entry->report_policy != CLOUD_REPORT_ON_CHANGE) ||
            (entry->base.get == NULL))
        {
            continue;
        }

        if (entry->base.get(&now) != SW_OK)
        {
            continue;
        }

        if (!s_shadow[i].valid)
        {
            s_shadow[i].valid = true;
            s_shadow[i].type  = entry->base.type;
            s_shadow[i].value = now;
            publish_dirty(i);
            continue;
        }

        if (!value_equal(entry->base.type, &s_shadow[i].value, &now))
        {
            s_shadow[i].value = now;
            publish_dirty(i);
        }
    }
}
