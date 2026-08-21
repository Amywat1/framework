/**
 * @file    engine_program_json_schema.c
 * @brief   方案 JSON 字段白名单校验
 * @author  huwangwei
 * @date    2026-08-21
 */

#include "adapters/outbound/storage/json/engine_program_json_internal.h"

#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static bool path_field(char *out, size_t out_size, const char *base, const char *field)
{
    return snprintf(out, out_size, "%s.%s", base, field) < (int)out_size;
}

static bool path_index(char *out, size_t out_size, const char *base, int index)
{
    return snprintf(out, out_size, "%s[%d]", base, index) < (int)out_size;
}

static bool path_field_index(char *out, size_t out_size, const char *base, const char *field, int index)
{
    size_t base_len;
    int    written;

    if ((out == NULL) || (out_size == 0U) || (base == NULL) || (field == NULL)) {
        return false;
    }
    base_len = strlen(base);
    if (base_len >= out_size) {
        return false;
    }
    (void)memcpy(out, base, base_len);
    written = snprintf(out + base_len, out_size - base_len, ".%s[%d]", field, index);
    return (written >= 0) && ((size_t)written < (out_size - base_len));
}

static bool object_fields_allowed(const cJSON       *node,
                                  const char *const *allowed,
                                  size_t             allowed_count,
                                  const char        *path,
                                  char              *err,
                                  unsigned           errsz)
{
    const cJSON *field = NULL;

    if (!cJSON_IsObject(node)) {
        jfail(err, errsz, "应为对象: %s", path);
        return false;
    }

    cJSON_ArrayForEach(field, node)
    {
        bool known = false;

        for (size_t i = 0U; i < allowed_count; ++i) {
            if ((field->string != NULL) && (strcmp(field->string, allowed[i]) == 0)) {
                known = true;
                break;
            }
        }
        if (!known) {
            char field_path[320];

            if (!path_field(field_path, sizeof(field_path), path, field->string != NULL ? field->string : "?")) {
                jfail(err, errsz, "字段路径过长: %s", path);
            } else {
                jfail(err, errsz, "不支持的字段: %s", field_path);
            }
            return false;
        }
    }
    return true;
}

static bool validate_intent_schema(const cJSON *node, const char *path, char *err, unsigned errsz)
{
    static const char *const allowed[] = {"resource", "cmd", "dir", "gear", "paths"};

    return object_fields_allowed(node, allowed, ARRAY_SIZE(allowed), path, err, errsz);
}

static bool validate_actions_schema(const cJSON *node, const char *path, char *err, unsigned errsz)
{
    static const char *const wrapper_allowed[] = {"act", "wait_time"};
    static const char *const wait_allowed[]    = {"ms"};

    if (node == NULL) {
        return true;
    }
    if (!cJSON_IsArray(node)) {
        jfail(err, errsz, "应为数组: %s", path);
        return false;
    }

    for (int i = 0; i < cJSON_GetArraySize(node); ++i) {
        const cJSON *item = cJSON_GetArrayItem(node, i);
        const cJSON *act;
        const cJSON *wait_time;
        char         item_path[320];
        char         child_path[320];

        if (!path_index(item_path, sizeof(item_path), path, i)) {
            jfail(err, errsz, "字段路径过长: %s", path);
            return false;
        }
        if (!object_fields_allowed(item, wrapper_allowed, ARRAY_SIZE(wrapper_allowed), item_path, err, errsz)) {
            return false;
        }

        act       = cJSON_GetObjectItemCaseSensitive(item, "act");
        wait_time = cJSON_GetObjectItemCaseSensitive(item, "wait_time");
        if ((act == NULL) == (wait_time == NULL)) {
            jfail(err, errsz, "动作必须且只能包含 act/wait_time 之一: %s", item_path);
            return false;
        }
        if (act != NULL) {
            (void)path_field(child_path, sizeof(child_path), item_path, "act");
            if (!validate_intent_schema(act, child_path, err, errsz)) {
                return false;
            }
        } else {
            (void)path_field(child_path, sizeof(child_path), item_path, "wait_time");
            if (!object_fields_allowed(wait_time, wait_allowed, ARRAY_SIZE(wait_allowed), child_path, err, errsz)) {
                return false;
            }
        }
    }
    return true;
}

static bool validate_step_schema(const cJSON *node, const char *path, char *err, unsigned errsz)
{
    static const char *const step_allowed[] = {
        "id",
        "type",
        "use",
        "active_while",
        "intent",
        "trigger",
        "guard",
        "actions",
        "done",
        "on_error",
        "retry_max",
        "after",
    };
    static const char *const trigger_allowed[] = {"type", "expr", "signal", "edge"};
    static const char *const done_allowed[]    = {"type", "signal", "state", "timeout_ms", "confirm_ms", "resource"};
    const cJSON             *child;
    char                     child_path[320];

    if (!object_fields_allowed(node, step_allowed, ARRAY_SIZE(step_allowed), path, err, errsz)) {
        return false;
    }

    child = cJSON_GetObjectItemCaseSensitive(node, "intent");
    if (child != NULL) {
        (void)path_field(child_path, sizeof(child_path), path, "intent");
        if (!validate_intent_schema(child, child_path, err, errsz)) {
            return false;
        }
    }
    child = cJSON_GetObjectItemCaseSensitive(node, "trigger");
    if (child != NULL) {
        (void)path_field(child_path, sizeof(child_path), path, "trigger");
        if (!object_fields_allowed(child, trigger_allowed, ARRAY_SIZE(trigger_allowed), child_path, err, errsz)) {
            return false;
        }
    }
    child = cJSON_GetObjectItemCaseSensitive(node, "done");
    if (child != NULL) {
        (void)path_field(child_path, sizeof(child_path), path, "done");
        if (!object_fields_allowed(child, done_allowed, ARRAY_SIZE(done_allowed), child_path, err, errsz)) {
            return false;
        }
    }
    child = cJSON_GetObjectItemCaseSensitive(node, "actions");
    (void)path_field(child_path, sizeof(child_path), path, "actions");
    return validate_actions_schema(child, child_path, err, errsz);
}

bool engine_program_json_validate_schema(const cJSON *root, char *err, unsigned errsz)
{
    static const char *const root_allowed[]    = {"program"};
    static const char *const program_allowed[] = {
        "schema_version",
        "id",
        "name",
        "templates",
        "params",
        "axes",
        "markers",
        "interlocks",
        "phases",
    };
    static const char *const axis_allowed[]      = {"type", "encoder", "pulse_per_mm", "direction"};
    static const char *const marker_allowed[]    = {"type", "axis", "on"};
    static const char *const marker_on_allowed[] = {"signal", "condition", "edge"};
    static const char *const interlock_allowed[] = {
        "id",
        "condition",
        "action",
        "actions",
        "priority",
        "reset_condition",
        "auto_reset",
    };
    static const char *const phase_allowed[] = {
        "id",
        "name",
        "direction",
        "entry_guard",
        "exit_guard",
        "timeout_ms",
        "on_timeout",
        "on_enter",
        "on_exit",
        "keep",
        "lanes",
    };
    static const char *const lane_allowed[] = {"id", "steps"};
    const cJSON             *program;
    const cJSON             *node;
    char                     path[320];

    if (!object_fields_allowed(root, root_allowed, ARRAY_SIZE(root_allowed), "root", err, errsz)) {
        return false;
    }
    program = cJSON_GetObjectItemCaseSensitive(root, "program");
    if (!object_fields_allowed(program, program_allowed, ARRAY_SIZE(program_allowed), "program", err, errsz)) {
        return false;
    }

    node = cJSON_GetObjectItemCaseSensitive(program, "params");
    if ((node != NULL) && !cJSON_IsObject(node)) {
        jfail(err, errsz, "应为对象: %s", "program.params");
        return false;
    }

    node = cJSON_GetObjectItemCaseSensitive(program, "axes");
    if (node != NULL) {
        const cJSON *item = NULL;

        if (!cJSON_IsObject(node)) {
            jfail(err, errsz, "应为对象: %s", "program.axes");
            return false;
        }
        cJSON_ArrayForEach(item, node)
        {
            (void)path_field(path, sizeof(path), "program.axes", item->string);
            if (!object_fields_allowed(item, axis_allowed, ARRAY_SIZE(axis_allowed), path, err, errsz)) {
                return false;
            }
        }
    }

    node = cJSON_GetObjectItemCaseSensitive(program, "markers");
    if (node != NULL) {
        const cJSON *item = NULL;

        if (!cJSON_IsObject(node)) {
            jfail(err, errsz, "应为对象: %s", "program.markers");
            return false;
        }
        cJSON_ArrayForEach(item, node)
        {
            const cJSON *on;
            char         on_path[320];

            (void)path_field(path, sizeof(path), "program.markers", item->string);
            if (!object_fields_allowed(item, marker_allowed, ARRAY_SIZE(marker_allowed), path, err, errsz)) {
                return false;
            }
            on = cJSON_GetObjectItemCaseSensitive(item, "on");
            (void)path_field(on_path, sizeof(on_path), path, "on");
            if (!object_fields_allowed(on, marker_on_allowed, ARRAY_SIZE(marker_on_allowed), on_path, err, errsz)) {
                return false;
            }
        }
    }

    node = cJSON_GetObjectItemCaseSensitive(program, "templates");
    if (node != NULL) {
        const cJSON *item  = NULL;
        int          index = 0;

        if (!cJSON_IsObject(node) && !cJSON_IsArray(node)) {
            jfail(err, errsz, "应为对象或数组: %s", "program.templates");
            return false;
        }
        cJSON_ArrayForEach(item, node)
        {
            if (cJSON_IsObject(node)) {
                (void)path_field(path, sizeof(path), "program.templates", item->string);
            } else {
                (void)path_index(path, sizeof(path), "program.templates", index);
            }
            if (!validate_step_schema(item, path, err, errsz)) {
                return false;
            }
            ++index;
        }
    }

    node = cJSON_GetObjectItemCaseSensitive(program, "interlocks");
    if (node != NULL) {
        if (!cJSON_IsArray(node)) {
            jfail(err, errsz, "应为数组: %s", "program.interlocks");
            return false;
        }
        for (int i = 0; i < cJSON_GetArraySize(node); ++i) {
            const cJSON *item = cJSON_GetArrayItem(node, i);
            const cJSON *actions;
            char         actions_path[320];

            (void)path_index(path, sizeof(path), "program.interlocks", i);
            if (!object_fields_allowed(item, interlock_allowed, ARRAY_SIZE(interlock_allowed), path, err, errsz)) {
                return false;
            }
            actions = cJSON_GetObjectItemCaseSensitive(item, "actions");
            (void)path_field(actions_path, sizeof(actions_path), path, "actions");
            if (!validate_actions_schema(actions, actions_path, err, errsz)) {
                return false;
            }
        }
    }

    node = cJSON_GetObjectItemCaseSensitive(program, "phases");
    if ((node != NULL) && !cJSON_IsArray(node)) {
        jfail(err, errsz, "应为数组: %s", "program.phases");
        return false;
    }
    for (int i = 0; (node != NULL) && (i < cJSON_GetArraySize(node)); ++i) {
        const cJSON *phase = cJSON_GetArrayItem(node, i);
        const cJSON *lanes;
        const cJSON *actions;
        char         child_path[320];

        (void)path_index(path, sizeof(path), "program.phases", i);
        if (!object_fields_allowed(phase, phase_allowed, ARRAY_SIZE(phase_allowed), path, err, errsz)) {
            return false;
        }
        actions = cJSON_GetObjectItemCaseSensitive(phase, "on_enter");
        (void)path_field(child_path, sizeof(child_path), path, "on_enter");
        if (!validate_actions_schema(actions, child_path, err, errsz)) {
            return false;
        }
        actions = cJSON_GetObjectItemCaseSensitive(phase, "on_exit");
        (void)path_field(child_path, sizeof(child_path), path, "on_exit");
        if (!validate_actions_schema(actions, child_path, err, errsz)) {
            return false;
        }

        lanes = cJSON_GetObjectItemCaseSensitive(phase, "lanes");
        if ((lanes != NULL) && !cJSON_IsArray(lanes)) {
            (void)path_field(child_path, sizeof(child_path), path, "lanes");
            jfail(err, errsz, "应为数组: %s", child_path);
            return false;
        }
        for (int lane_index = 0; (lanes != NULL) && (lane_index < cJSON_GetArraySize(lanes)); ++lane_index) {
            const cJSON *lane = cJSON_GetArrayItem(lanes, lane_index);
            const cJSON *steps;
            char         lane_path[320];

            if (!path_field_index(lane_path, sizeof(lane_path), path, "lanes", lane_index)) {
                jfail(err, errsz, "字段路径过长: %s", path);
                return false;
            }
            if (!object_fields_allowed(lane, lane_allowed, ARRAY_SIZE(lane_allowed), lane_path, err, errsz)) {
                return false;
            }
            steps = cJSON_GetObjectItemCaseSensitive(lane, "steps");
            if ((steps != NULL) && !cJSON_IsArray(steps)) {
                (void)path_field(child_path, sizeof(child_path), lane_path, "steps");
                jfail(err, errsz, "应为数组: %s", child_path);
                return false;
            }
            for (int step_index = 0; (steps != NULL) && (step_index < cJSON_GetArraySize(steps)); ++step_index) {
                if (!path_field_index(child_path, sizeof(child_path), lane_path, "steps", step_index)) {
                    jfail(err, errsz, "字段路径过长: %s", lane_path);
                    return false;
                }
                if (!validate_step_schema(cJSON_GetArrayItem(steps, step_index), child_path, err, errsz)) {
                    return false;
                }
            }
        }
    }
    return true;
}
