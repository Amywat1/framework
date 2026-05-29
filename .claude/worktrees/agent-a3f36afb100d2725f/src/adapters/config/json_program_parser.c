#include "adapters/config/json_program_parser.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "domain/model/program_validation.h"
#include "shared/error_codes.h"

/* 保持与旧实现一致的资源边界，避免受限进程吃下超大配置。 */
#define JSON_MAX_DOCUMENT_BYTES 16383L
#define JSON_MAX_NESTING_DEPTH 32

typedef enum json_expected_type_t
{
    JSON_EXPECT_OBJECT = 0,
    JSON_EXPECT_ARRAY,
    JSON_EXPECT_STRING,
    JSON_EXPECT_NUMBER,
    JSON_EXPECT_BOOL
} json_expected_type_t;

/**
 * @brief 判断 JSON 值是否符合期望类型
 * @param json_value 待检查的 JSON 值
 * @param expected_type 期望类型
 * @return true 表示类型匹配；null 或类型不符时返回 false
 */
static bool json_matches_expected_type(const cJSON *json_value, json_expected_type_t expected_type)
{
    if (json_value == 0)
    {
        return false;
    }

    switch (expected_type)
    {
    case JSON_EXPECT_OBJECT:
        return cJSON_IsObject(json_value) != 0;
    case JSON_EXPECT_ARRAY:
        return cJSON_IsArray(json_value) != 0;
    case JSON_EXPECT_STRING:
        return cJSON_IsString(json_value) != 0;
    case JSON_EXPECT_NUMBER:
        return cJSON_IsNumber(json_value) != 0;
    case JSON_EXPECT_BOOL:
        return cJSON_IsBool(json_value) != 0;
    default:
        return false;
    }
}

/**
 * @brief 读取配置文件内容到动态分配的缓冲区
 * @param config_path 配置文件路径
 * @param[out] text_buffer 指向缓冲区指针的指针；调用方负责 free()
 * @return 读取成功返回 ok；文件不存在、超过限制、I/O 失败时返回相应错误
 * @details 检查文件大小（JSON_MAX_DOCUMENT_BYTES），保证 null 终止符
 */
static operation_result_t read_file_text(const char *config_path, char **text_buffer)
{
    FILE *file_handle;
    long file_length;
    size_t read_size;
    char *buffer;

    if (config_path == 0 || text_buffer == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *text_buffer = 0;

    file_handle = fopen(config_path, "rb");
    if (file_handle == 0)
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (fseek(file_handle, 0, SEEK_END) != 0)
    {
        fclose(file_handle);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    file_length = ftell(file_handle);
    /* 检查文件大小：必须在 1 到 JSON_MAX_DOCUMENT_BYTES 之间（DoS 防护） */
    if (file_length <= 0 || file_length > JSON_MAX_DOCUMENT_BYTES)
    {
        fclose(file_handle);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (fseek(file_handle, 0, SEEK_SET) != 0)
    {
        fclose(file_handle);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    buffer = (char *)malloc((size_t)file_length + 1u);
    if (buffer == 0)
    {
        fclose(file_handle);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    read_size = fread(buffer, 1u, (size_t)file_length, file_handle);
    fclose(file_handle);
    if (read_size != (size_t)file_length)
    {
        free(buffer);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    buffer[file_length] = '\0';
    *text_buffer = buffer;
    return operation_result_ok();
}

/**
 * @brief 检查 JSON 对象是否存在重复的键
 * @param json_object JSON 对象
 * @return true 表示存在重复键；不是对象时返回 false
 * @details O(n²) 扫描；用于配置验证（禁止歧义的键定义）
 */
static bool json_object_has_duplicate_keys(const cJSON *json_object)
{
    const cJSON *member;

    if (!cJSON_IsObject(json_object))
    {
        return false;
    }

    for (member = json_object->child; member != 0; member = member->next)
    {
        const cJSON *other_member;

        if (member->string == 0)
        {
            return true;
        }
        for (other_member = member->next; other_member != 0; other_member = other_member->next)
        {
            if (other_member->string != 0 && strcmp(member->string, other_member->string) == 0)
            {
                return true;
            }
        }
    }

    return false;
}

static operation_result_t validate_json_tree_constraints(const cJSON *json_value, int depth)
{
    const cJSON *child;

    if (json_value == 0)
    {
        return operation_result_ok();
    }
    if (depth > JSON_MAX_NESTING_DEPTH)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    if (json_object_has_duplicate_keys(json_value))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    for (child = json_value->child; child != 0; child = child->next)
    {
        operation_result_t result = validate_json_tree_constraints(child, depth + 1);
        if (!result.ok)
        {
            return result;
        }
    }

    return operation_result_ok();
}

static operation_result_t require_member(const cJSON *json_object, const char *key, json_expected_type_t expected_type,
                                         const cJSON **member_value)
{
    const cJSON *found_value;

    if (json_object == 0 || key == 0 || member_value == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    found_value = cJSON_GetObjectItemCaseSensitive(json_object, key);
    if (!json_matches_expected_type(found_value, expected_type))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    *member_value = found_value;
    return operation_result_ok();
}

static operation_result_t optional_member(const cJSON *json_object, const char *key, json_expected_type_t expected_type,
                                          const cJSON **member_value, bool *present)
{
    const cJSON *found_value;

    if (json_object == 0 || key == 0 || member_value == 0 || present == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    found_value = cJSON_GetObjectItemCaseSensitive(json_object, key);
    if (found_value == 0)
    {
        *member_value = 0;
        *present = false;
        return operation_result_ok();
    }
    if (!json_matches_expected_type(found_value, expected_type))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    *member_value = found_value;
    *present = true;
    return operation_result_ok();
}

static operation_result_t copy_string_value(const cJSON *json_value, char *target, size_t target_size)
{
    size_t length;

    if (!cJSON_IsString(json_value) || json_value->valuestring == 0 || target == 0 || target_size == 0u)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    length = strlen(json_value->valuestring);
    if (length >= target_size)
    {
        length = target_size - 1u;
    }
    memcpy(target, json_value->valuestring, length);
    target[length] = '\0';
    return operation_result_ok();
}

static operation_result_t copy_required_string_member(const cJSON *json_object, const char *key, char *target,
                                                      size_t target_size)
{
    const cJSON *member_value;
    operation_result_t result;

    result = require_member(json_object, key, JSON_EXPECT_STRING, &member_value);
    if (!result.ok)
    {
        return result;
    }
    return copy_string_value(member_value, target, target_size);
}

static operation_result_t copy_optional_string_member(const cJSON *json_object, const char *key, char *target,
                                                      size_t target_size, bool *present)
{
    const cJSON *member_value;
    operation_result_t result;

    result = optional_member(json_object, key, JSON_EXPECT_STRING, &member_value, present);
    if (!result.ok || !*present)
    {
        return result;
    }
    return copy_string_value(member_value, target, target_size);
}

static operation_result_t read_int_value(const cJSON *json_value, int *value)
{
    double integer_part;

    if (!cJSON_IsNumber(json_value) || value == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    if (!isfinite(json_value->valuedouble))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    if (modf(json_value->valuedouble, &integer_part) != 0.0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    if (integer_part < (double)INT_MIN || integer_part > (double)INT_MAX)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    *value = (int)integer_part;
    return operation_result_ok();
}

static operation_result_t read_required_int_member(const cJSON *json_object, const char *key, int *value)
{
    const cJSON *member_value;
    operation_result_t result;

    result = require_member(json_object, key, JSON_EXPECT_NUMBER, &member_value);
    if (!result.ok)
    {
        return result;
    }
    return read_int_value(member_value, value);
}

static operation_result_t read_optional_int_member(const cJSON *json_object, const char *key, int *value, bool *present)
{
    const cJSON *member_value;
    operation_result_t result;

    result = optional_member(json_object, key, JSON_EXPECT_NUMBER, &member_value, present);
    if (!result.ok || !*present)
    {
        return result;
    }
    return read_int_value(member_value, value);
}

static operation_result_t read_optional_bool_member(const cJSON *json_object, const char *key, bool *value,
                                                    bool *present)
{
    const cJSON *member_value;
    operation_result_t result;

    result = optional_member(json_object, key, JSON_EXPECT_BOOL, &member_value, present);
    if (!result.ok)
    {
        return result;
    }
    if (*present && value != 0)
    {
        *value = cJSON_IsTrue(member_value) != 0;
    }
    return operation_result_ok();
}

static bool parse_exception_strategy(const char *text, exception_strategy_t *exception_strategy)
{
    if (strcmp(text, "abort_session") == 0)
    {
        *exception_strategy = EXCEPTION_STRATEGY_ABORT_SESSION;
        return true;
    }
    if (strcmp(text, "safe_finish") == 0)
    {
        *exception_strategy = EXCEPTION_STRATEGY_SAFE_FINISH;
        return true;
    }
    return false;
}

static bool parse_segment_kind(const char *text, segment_kind_t *segment_kind)
{
    if (strcmp(text, "roof_brush") == 0)
    {
        *segment_kind = SEGMENT_KIND_ROOF_BRUSH;
        return true;
    }
    if (strcmp(text, "side_brush") == 0)
    {
        *segment_kind = SEGMENT_KIND_SIDE_BRUSH;
        return true;
    }
    if (strcmp(text, "ro_water") == 0)
    {
        *segment_kind = SEGMENT_KIND_RO_WATER;
        return true;
    }
    if (strcmp(text, "dryer") == 0)
    {
        *segment_kind = SEGMENT_KIND_DRYER;
        return true;
    }
    return false;
}

static bool parse_motion_direction(const char *text, motion_direction_t *motion_direction)
{
    if (strcmp(text, "forward") == 0)
    {
        *motion_direction = MOTION_DIRECTION_FORWARD;
        return true;
    }
    if (strcmp(text, "reverse") == 0)
    {
        *motion_direction = MOTION_DIRECTION_REVERSE;
        return true;
    }
    if (strcmp(text, "stop") == 0)
    {
        *motion_direction = MOTION_DIRECTION_STOP;
        return true;
    }
    return false;
}

static bool parse_motion_target(const char *text, motion_target_reference_t *motion_target_reference)
{
    if (strcmp(text, "head") == 0)
    {
        *motion_target_reference = MOTION_TARGET_HEAD;
        return true;
    }
    if (strcmp(text, "tail") == 0)
    {
        *motion_target_reference = MOTION_TARGET_TAIL;
        return true;
    }
    if (strcmp(text, "home") == 0)
    {
        *motion_target_reference = MOTION_TARGET_HOME;
        return true;
    }
    if (strcmp(text, "relative_distance") == 0)
    {
        *motion_target_reference = MOTION_TARGET_RELATIVE_DISTANCE;
        return true;
    }
    if (strcmp(text, "absolute_position") == 0)
    {
        *motion_target_reference = MOTION_TARGET_ABSOLUTE_POSITION;
        return true;
    }
    return false;
}

static bool parse_position_reference(const char *text, position_reference_t *position_reference)
{
    if (strcmp(text, "absolute_mm") == 0)
    {
        *position_reference = POSITION_REFERENCE_ABSOLUTE_MM;
        return true;
    }
    if (strcmp(text, "distance_to_head_mm") == 0)
    {
        *position_reference = POSITION_REFERENCE_DISTANCE_TO_HEAD_MM;
        return true;
    }
    if (strcmp(text, "distance_to_tail_mm") == 0)
    {
        *position_reference = POSITION_REFERENCE_DISTANCE_TO_TAIL_MM;
        return true;
    }
    if (strcmp(text, "head_reached") == 0)
    {
        *position_reference = POSITION_REFERENCE_HEAD_REACHED;
        return true;
    }
    if (strcmp(text, "tail_reached") == 0)
    {
        *position_reference = POSITION_REFERENCE_TAIL_REACHED;
        return true;
    }
    if (strcmp(text, "home_reached") == 0)
    {
        *position_reference = POSITION_REFERENCE_HOME_REACHED;
        return true;
    }
    return false;
}

static bool parse_compare_mode(const char *text, position_compare_mode_t *position_compare_mode)
{
    if (strcmp(text, "true") == 0)
    {
        *position_compare_mode = POSITION_COMPARE_TRUE;
        return true;
    }
    if (strcmp(text, "greater_equal") == 0)
    {
        *position_compare_mode = POSITION_COMPARE_GREATER_EQUAL;
        return true;
    }
    if (strcmp(text, "less_equal") == 0)
    {
        *position_compare_mode = POSITION_COMPARE_LESS_EQUAL;
        return true;
    }
    if (strcmp(text, "range_inclusive") == 0)
    {
        *position_compare_mode = POSITION_COMPARE_RANGE_INCLUSIVE;
        return true;
    }
    return false;
}

static operation_result_t parse_position_trigger_object(const cJSON *json_value, position_trigger_t *position_trigger)
{
    char text[64];
    bool present;
    operation_result_t result;

    if (!cJSON_IsObject(json_value) || position_trigger == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    position_trigger_init(position_trigger);
    result = copy_required_string_member(json_value, "reference", text, sizeof(text));
    if (!result.ok || !parse_position_reference(text, &position_trigger->reference))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_required_string_member(json_value, "compare_mode", text, sizeof(text));
    if (!result.ok || !parse_compare_mode(text, &position_trigger->compare_mode))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = read_optional_int_member(json_value, "value_mm", &position_trigger->value_mm, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_int_member(json_value, "upper_value_mm", &position_trigger->upper_value_mm, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_int_member(json_value, "tolerance_mm", &position_trigger->tolerance_mm, &present);
    if (!result.ok)
    {
        return result;
    }

    return position_trigger_is_valid(position_trigger) ? operation_result_ok()
                                                       : operation_result_fail(ERROR_CODE_PARSE_FAILED);
}

static operation_result_t parse_motion_plan(const cJSON *segment_value, segment_motion_plan_t *segment_motion_plan)
{
    const cJSON *motion_plan_value;
    char text[64];
    bool present;
    operation_result_t result;

    if (segment_motion_plan == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    segment_motion_plan_init(segment_motion_plan);
    result = require_member(segment_value, "motion_plan", JSON_EXPECT_OBJECT, &motion_plan_value);
    if (!result.ok)
    {
        return result;
    }

    result = copy_required_string_member(motion_plan_value, "direction", text, sizeof(text));
    if (!result.ok || !parse_motion_direction(text, &segment_motion_plan->direction))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_required_string_member(motion_plan_value, "target_reference", text, sizeof(text));
    if (!result.ok || !parse_motion_target(text, &segment_motion_plan->target_reference))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_optional_string_member(motion_plan_value, "relative_basis", text, sizeof(text), &present);
    if (!result.ok)
    {
        return result;
    }
    if (present && !parse_position_reference(text, &segment_motion_plan->relative_basis))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = read_optional_int_member(motion_plan_value, "target_distance_mm", &segment_motion_plan->target_distance_mm,
                                      &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_int_member(motion_plan_value, "target_tolerance_mm",
                                      &segment_motion_plan->target_tolerance_mm, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(motion_plan_value, "requires_position_feedback",
                                       &segment_motion_plan->requires_position_feedback, &present);
    if (!result.ok)
    {
        return result;
    }
    if (!present)
    {
        segment_motion_plan->requires_position_feedback = true;
    }

    return segment_motion_plan_is_valid(segment_motion_plan) ? operation_result_ok()
                                                             : operation_result_fail(ERROR_CODE_PARSE_FAILED);
}

static operation_result_t parse_continuous_controls(const cJSON *segment_value,
                                                    segment_continuous_controls_t *segment_continuous_controls)
{
    const cJSON *controls_value;
    bool present;
    operation_result_t result;

    if (segment_continuous_controls == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    memset(segment_continuous_controls, 0, sizeof(*segment_continuous_controls));
    result = require_member(segment_value, "continuous_controls", JSON_EXPECT_OBJECT, &controls_value);
    if (!result.ok)
    {
        return result;
    }

    result = read_optional_bool_member(controls_value, "roof_brush_follow",
                                       &segment_continuous_controls->roof_brush_follow, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(controls_value, "side_brush_enabled",
                                       &segment_continuous_controls->side_brush_enabled, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(controls_value, "ro_water_enabled",
                                       &segment_continuous_controls->ro_water_enabled, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(controls_value, "dryer_enabled", &segment_continuous_controls->dryer_enabled,
                                       &present);
    if (!result.ok)
    {
        return result;
    }
    return operation_result_ok();
}

static operation_result_t parse_conditional_controls(const cJSON *segment_value, wash_segment_t *wash_segment)
{
    const cJSON *conditional_controls_value;
    const cJSON *control_value;
    bool present;
    operation_result_t result;

    if (wash_segment == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    wash_segment->conditional_control_count = 0;
    result = optional_member(segment_value, "conditional_controls", JSON_EXPECT_ARRAY, &conditional_controls_value,
                             &present);
    if (!result.ok)
    {
        return result;
    }
    if (!present)
    {
        return operation_result_ok();
    }

    cJSON_ArrayForEach(control_value, conditional_controls_value)
    {
        conditional_control_t *conditional_control;
        const cJSON *trigger_value;
        char text[64];

        if (!cJSON_IsObject(control_value))
        {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        if (wash_segment->conditional_control_count >= MAX_SEGMENT_CONDITIONAL_CONTROLS)
        {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        conditional_control = &wash_segment->conditional_controls[wash_segment->conditional_control_count];
        conditional_control_init(conditional_control);
        result = copy_required_string_member(control_value, "control_id", conditional_control->control_id,
                                             sizeof(conditional_control->control_id));
        if (!result.ok)
        {
            return result;
        }
        result = copy_required_string_member(control_value, "control_object", text, sizeof(text));
        if (!result.ok)
        {
            return result;
        }
        if (strcmp(text, "chemical") != 0)
        {
            return operation_result_fail(ERROR_CODE_UNSUPPORTED);
        }

        conditional_control->kind = CONDITIONAL_CONTROL_CHEMICAL_WINDOW;
        conditional_control->control_object = ACTUATOR_CHEMICAL;
        result = copy_required_string_member(control_value, "basis", text, sizeof(text));
        if (!result.ok || !parse_position_reference(text, &conditional_control->basis))
        {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        result = require_member(control_value, "start_trigger", JSON_EXPECT_OBJECT, &trigger_value);
        if (!result.ok)
        {
            return result;
        }
        result = parse_position_trigger_object(trigger_value, &conditional_control->start_trigger);
        if (!result.ok)
        {
            return result;
        }
        result = require_member(control_value, "stop_trigger", JSON_EXPECT_OBJECT, &trigger_value);
        if (!result.ok)
        {
            return result;
        }
        result = parse_position_trigger_object(trigger_value, &conditional_control->stop_trigger);
        if (!result.ok)
        {
            return result;
        }
        result = read_optional_bool_member(control_value, "active_during_exit",
                                           &conditional_control->active_during_exit, &present);
        if (!result.ok)
        {
            return result;
        }
        if (!present)
        {
            conditional_control->active_during_exit = false;
        }
        if (!conditional_control_is_valid(conditional_control))
        {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        wash_segment->conditional_control_count += 1;
    }

    return operation_result_ok();
}

static operation_result_t parse_completion_condition(const cJSON *segment_value,
                                                     segment_completion_condition_t *segment_completion_condition)
{
    const cJSON *completion_value;
    operation_result_t result;

    if (segment_completion_condition == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    memset(segment_completion_condition, 0, sizeof(*segment_completion_condition));
    result = require_member(segment_value, "completion_condition", JSON_EXPECT_OBJECT, &completion_value);
    if (!result.ok)
    {
        return result;
    }
    return parse_position_trigger_object(completion_value, &segment_completion_condition->trigger);
}

static operation_result_t parse_exit_actions(const cJSON *segment_value, segment_exit_actions_t *segment_exit_actions)
{
    const cJSON *exit_actions_value;
    bool present;
    operation_result_t result;

    if (segment_exit_actions == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    segment_exit_actions_init(segment_exit_actions);
    result = require_member(segment_value, "exit_actions", JSON_EXPECT_OBJECT, &exit_actions_value);
    if (!result.ok)
    {
        return result;
    }

    result = read_optional_bool_member(exit_actions_value, "stop_roof_brush", &segment_exit_actions->stop_roof_brush,
                                       &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "stop_side_brush", &segment_exit_actions->stop_side_brush,
                                       &present);
    if (!result.ok)
    {
        return result;
    }
    result =
        read_optional_bool_member(exit_actions_value, "stop_chemical", &segment_exit_actions->stop_chemical, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "close_ro_water", &segment_exit_actions->close_ro_water,
                                       &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "close_dryer", &segment_exit_actions->close_dryer, &present);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "roof_brush_home", &segment_exit_actions->roof_brush_home,
                                       &present);
    if (!result.ok)
    {
        return result;
    }
    return operation_result_ok();
}

static operation_result_t parse_exception_policy(const cJSON *segment_value,
                                                 segment_exception_policy_t *segment_exception_policy)
{
    const cJSON *exception_policy_value;
    bool present;
    char text[64];
    operation_result_t result;

    if (segment_exception_policy == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    segment_exception_policy_init(segment_exception_policy);
    result = optional_member(segment_value, "exception_policy", JSON_EXPECT_OBJECT, &exception_policy_value, &present);
    if (!result.ok)
    {
        return result;
    }
    if (!present)
    {
        return operation_result_ok();
    }

    result = copy_optional_string_member(exception_policy_value, "on_position_lost", text, sizeof(text), &present);
    if (!result.ok)
    {
        return result;
    }
    if (present && !parse_exception_strategy(text, &segment_exception_policy->on_position_lost))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_optional_string_member(exception_policy_value, "on_follow_lost", text, sizeof(text), &present);
    if (!result.ok)
    {
        return result;
    }
    if (present && !parse_exception_strategy(text, &segment_exception_policy->on_follow_lost))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_optional_string_member(exception_policy_value, "on_segment_timeout", text, sizeof(text), &present);
    if (!result.ok)
    {
        return result;
    }
    if (present && !parse_exception_strategy(text, &segment_exception_policy->on_segment_timeout))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_optional_string_member(exception_policy_value, "on_exit_timeout", text, sizeof(text), &present);
    if (!result.ok)
    {
        return result;
    }
    if (present && !parse_exception_strategy(text, &segment_exception_policy->on_exit_timeout))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_optional_string_member(exception_policy_value, "on_exit_failure", text, sizeof(text), &present);
    if (!result.ok)
    {
        return result;
    }
    if (present && !parse_exception_strategy(text, &segment_exception_policy->on_exit_failure))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    return operation_result_ok();
}

static operation_result_t parse_segment_object(const cJSON *segment_value, wash_program_t *wash_program)
{
    char text[64];
    wash_segment_t wash_segment;
    bool present;
    operation_result_t result;

    if (!cJSON_IsObject(segment_value) || wash_program == 0)
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    wash_segment_init(&wash_segment, "", "", 0);
    result = copy_required_string_member(segment_value, "segment_id", wash_segment.segment_id,
                                         sizeof(wash_segment.segment_id));
    if (!result.ok)
    {
        return result;
    }
    result = copy_required_string_member(segment_value, "segment_name", wash_segment.segment_name,
                                         sizeof(wash_segment.segment_name));
    if (!result.ok)
    {
        return result;
    }
    result = read_required_int_member(segment_value, "sequence_no", &wash_segment.sequence_no);
    if (!result.ok)
    {
        return result;
    }
    result = copy_required_string_member(segment_value, "segment_kind", text, sizeof(text));
    if (!result.ok || !parse_segment_kind(text, &wash_segment.segment_kind))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = parse_motion_plan(segment_value, &wash_segment.motion_plan);
    if (!result.ok)
    {
        return result;
    }
    result = parse_continuous_controls(segment_value, &wash_segment.continuous_controls);
    if (!result.ok)
    {
        return result;
    }
    result = parse_conditional_controls(segment_value, &wash_segment);
    if (!result.ok)
    {
        return result;
    }
    result = parse_completion_condition(segment_value, &wash_segment.completion_condition);
    if (!result.ok)
    {
        return result;
    }
    result = parse_exit_actions(segment_value, &wash_segment.exit_actions);
    if (!result.ok)
    {
        return result;
    }
    result = read_optional_int_member(segment_value, "segment_timeout_ms", &wash_segment.segment_timeout_ms, &present);
    if (!result.ok)
    {
        return result;
    }
    if (!present)
    {
        wash_segment.segment_timeout_ms = wash_program->default_segment_timeout_ms;
    }
    result = read_optional_int_member(segment_value, "exit_timeout_ms", &wash_segment.exit_timeout_ms, &present);
    if (!result.ok)
    {
        return result;
    }
    if (!present)
    {
        wash_segment.exit_timeout_ms = wash_program->default_exit_timeout_ms;
    }
    result = parse_exception_policy(segment_value, &wash_segment.exception_policy);
    if (!result.ok)
    {
        return result;
    }
    if (!wash_segment_is_valid(&wash_segment))
    {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    return wash_program_add_segment(wash_program, &wash_segment) == 0 ? operation_result_ok()
                                                                      : operation_result_fail(ERROR_CODE_PARSE_FAILED);
}

operation_result_t json_program_parser_parse(const char *config_path, wash_program_t *wash_program)
{
    char *json_text;
    cJSON *root_value;
    const cJSON *segments_value;
    const cJSON *segment_value;
    wash_program_t parsed_program;
    char program_id[sizeof(parsed_program.program_id)];
    char program_name[sizeof(parsed_program.program_name)];
    bool present;
    operation_result_t result;

    if (config_path == 0 || wash_program == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    json_text = 0;
    root_value = 0;
    result = read_file_text(config_path, &json_text);
    if (!result.ok)
    {
        return result;
    }

    root_value = cJSON_ParseWithOpts(json_text, 0, 1);
    if (root_value == 0 || !cJSON_IsObject(root_value))
    {
        result = operation_result_fail(ERROR_CODE_PARSE_FAILED);
        goto cleanup;
    }
    result = validate_json_tree_constraints(root_value, 0);
    if (!result.ok)
    {
        goto cleanup;
    }
    if (cJSON_GetObjectItemCaseSensitive(root_value, "stages") != 0)
    {
        result = operation_result_fail(ERROR_CODE_UNSUPPORTED);
        goto cleanup;
    }

    result = copy_required_string_member(root_value, "program_id", program_id, sizeof(program_id));
    if (!result.ok)
    {
        goto cleanup;
    }
    result = copy_required_string_member(root_value, "program_name", program_name, sizeof(program_name));
    if (!result.ok)
    {
        goto cleanup;
    }

    wash_program_init(&parsed_program, program_id, program_name);
    result = read_optional_int_member(root_value, "revision", &parsed_program.revision, &present);
    if (!result.ok)
    {
        goto cleanup;
    }
    result = read_optional_bool_member(root_value, "enabled", &parsed_program.enabled, &present);
    if (!result.ok)
    {
        goto cleanup;
    }
    result = read_optional_int_member(root_value, "default_segment_timeout_ms",
                                      &parsed_program.default_segment_timeout_ms, &present);
    if (!result.ok)
    {
        goto cleanup;
    }
    result = read_optional_int_member(root_value, "default_exit_timeout_ms", &parsed_program.default_exit_timeout_ms,
                                      &present);
    if (!result.ok)
    {
        goto cleanup;
    }
    result = require_member(root_value, "segments", JSON_EXPECT_ARRAY, &segments_value);
    if (!result.ok)
    {
        goto cleanup;
    }

    cJSON_ArrayForEach(segment_value, segments_value)
    {
        result = parse_segment_object(segment_value, &parsed_program);
        if (!result.ok)
        {
            goto cleanup;
        }
    }

    result = program_validation_validate(&parsed_program);
    if (!result.ok)
    {
        goto cleanup;
    }

    *wash_program = parsed_program;
    result = operation_result_ok();

cleanup:
    cJSON_Delete(root_value);
    free(json_text);
    return result;
}
