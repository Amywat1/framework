/**
 * @file    cloud_point_watcher.c
 * @brief   云端物模型 on_change 点位变更检测实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "domain/cloud/cloud_point_watcher.h"

#include <string.h>

typedef struct {
    bool          valid;
    point_type_t  type;
    point_value_t value;
} cloud_point_shadow_t;

static const cloud_point_entry_t *s_entries     = NULL;
static size_t                     s_entry_count = 0U;
static cloud_point_shadow_t       s_shadow[CLOUD_POINT_TABLE_MAX];
static bool                       s_dirty[CLOUD_POINT_TABLE_MAX];

static bool is_watched_entry(const cloud_point_entry_t *entry)
{
    return entry->on_change && (entry->base.get != NULL);
}

static bool value_equal(point_type_t type, const point_value_t *a, const point_value_t *b)
{
    switch (type) {
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

void cloud_point_watcher_reset_for_test(void)
{
    s_entries     = NULL;
    s_entry_count = 0U;
    memset(s_shadow, 0, sizeof(s_shadow));
    memset(s_dirty, 0, sizeof(s_dirty));
}

sw_err_t cloud_point_watcher_init(const cloud_point_entry_t *entries, size_t count)
{
    if ((entries == NULL) || (count == 0U) || (count > CLOUD_POINT_TABLE_MAX)) {
        return SW_ERR_PARAM;
    }

    memset(s_shadow, 0, sizeof(s_shadow));
    memset(s_dirty, 0, sizeof(s_dirty));
    s_entries     = entries;
    s_entry_count = count;

    for (size_t i = 0U; i < count; i++) {
        point_value_t val;

        if (!is_watched_entry(&entries[i])) {
            continue;
        }

        if (entries[i].base.get(&val) != SW_OK) {
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

    if (s_entries == NULL) {
        return;
    }

    for (i = 0U; i < s_entry_count; i++) {
        const cloud_point_entry_t *entry = &s_entries[i];
        point_value_t              now;

        if (!is_watched_entry(entry)) {
            continue;
        }

        if (entry->base.get(&now) != SW_OK) {
            continue;
        }

        if (!s_shadow[i].valid) {
            s_shadow[i].valid = true;
            s_shadow[i].type  = entry->base.type;
            s_shadow[i].value = now;
            s_dirty[i]        = true;
            continue;
        }

        if (!value_equal(entry->base.type, &s_shadow[i].value, &now)) {
            s_shadow[i].value = now;
            s_dirty[i]        = true;
        }
    }
}

size_t cloud_point_watcher_take_dirty(const char **ids, size_t cap)
{
    size_t n = 0U;
    size_t i;

    if ((ids == NULL) || (cap == 0U) || (s_entries == NULL)) {
        return 0U;
    }

    for (i = 0U; (i < s_entry_count) && (n < cap); i++) {
        if (!s_dirty[i]) {
            continue;
        }
        ids[n++]   = s_entries[i].base.id;
        s_dirty[i] = false;
    }

    return n;
}

void cloud_point_watcher_restore_dirty(const char *const *ids, size_t count)
{
    size_t i;
    size_t j;

    if ((ids == NULL) || (s_entries == NULL)) {
        return;
    }

    for (i = 0U; i < count; i++) {
        if (ids[i] == NULL) {
            continue;
        }
        for (j = 0U; j < s_entry_count; j++) {
            if ((s_entries[j].base.id != NULL) && (strcmp(s_entries[j].base.id, ids[i]) == 0)) {
                s_dirty[j] = true;
                break;
            }
        }
    }
}
