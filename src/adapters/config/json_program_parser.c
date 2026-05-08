#include "adapters/config/json_program_parser.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "domain/model/program_validation.h"
#include "shared/error_codes.h"

#define JSON_MAX_NESTING_DEPTH 32

typedef enum json_value_type_t {
    JSON_VALUE_OBJECT = 0,
    JSON_VALUE_ARRAY,
    JSON_VALUE_STRING,
    JSON_VALUE_NUMBER,
    JSON_VALUE_BOOL,
    JSON_VALUE_NULL
} json_value_type_t;

typedef struct json_value_t json_value_t;

typedef struct json_object_member_t {
    char *key;
    json_value_t *value;
} json_object_member_t;

struct json_value_t {
    json_value_type_t type;
    union {
        struct {
            json_object_member_t *items;
            size_t count;
        } object_value;
        struct {
            json_value_t **items;
            size_t count;
        } array_value;
        char *string_value;
        long number_value;
        bool bool_value;
    } data;
};

typedef struct json_parser_t {
    const char *cursor;
} json_parser_t;

static bool read_file_text(const char *config_path, char *buffer, size_t buffer_size)
{
    FILE *file_handle;
    long file_length;

    file_handle = fopen(config_path, "r");
    if (file_handle == 0) {
        return false;
    }
    fseek(file_handle, 0, SEEK_END);
    file_length = ftell(file_handle);
    fseek(file_handle, 0, SEEK_SET);
    if (file_length <= 0 || (size_t)file_length >= buffer_size) {
        fclose(file_handle);
        return false;
    }
    memset(buffer, 0, buffer_size);
    if (fread(buffer, 1, (size_t)file_length, file_handle) != (size_t)file_length) {
        fclose(file_handle);
        return false;
    }
    fclose(file_handle);
    return true;
}

static void json_value_init(json_value_t *json_value)
{
    if (json_value == 0) {
        return;
    }
    memset(json_value, 0, sizeof(*json_value));
}

static void json_value_free(json_value_t *json_value)
{
    size_t index;

    if (json_value == 0) {
        return;
    }

    switch (json_value->type) {
        case JSON_VALUE_OBJECT:
            for (index = 0; index < json_value->data.object_value.count; ++index) {
                free(json_value->data.object_value.items[index].key);
                json_value_free(json_value->data.object_value.items[index].value);
                free(json_value->data.object_value.items[index].value);
            }
            free(json_value->data.object_value.items);
            break;
        case JSON_VALUE_ARRAY:
            for (index = 0; index < json_value->data.array_value.count; ++index) {
                json_value_free(json_value->data.array_value.items[index]);
                free(json_value->data.array_value.items[index]);
            }
            free(json_value->data.array_value.items);
            break;
        case JSON_VALUE_STRING:
            free(json_value->data.string_value);
            break;
        case JSON_VALUE_NUMBER:
        case JSON_VALUE_BOOL:
        case JSON_VALUE_NULL:
        default:
            break;
    }

    json_value_init(json_value);
}

static void skip_whitespace(json_parser_t *json_parser)
{
    while (json_parser->cursor != 0 && *json_parser->cursor != '\0'
        && isspace((unsigned char)*json_parser->cursor)) {
        json_parser->cursor += 1;
    }
}

static bool decode_hex4(const char *cursor, unsigned int *value)
{
    int index;

    *value = 0;
    for (index = 0; index < 4; ++index) {
        char ch = cursor[index];
        *value <<= 4;
        if (ch >= '0' && ch <= '9') {
            *value |= (unsigned int)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            *value |= (unsigned int)(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            *value |= (unsigned int)(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}

static bool parse_json_string(json_parser_t *json_parser, char **value)
{
    char *buffer;
    size_t capacity;
    size_t length;

    if (json_parser == 0 || value == 0 || json_parser->cursor == 0 || *json_parser->cursor != '"') {
        return false;
    }

    json_parser->cursor += 1;
    capacity = strlen(json_parser->cursor) + 1;
    buffer = (char *)malloc(capacity);
    if (buffer == 0) {
        return false;
    }

    length = 0;
    while (*json_parser->cursor != '\0') {
        char ch = *json_parser->cursor;
        if (ch == '"') {
            buffer[length] = '\0';
            json_parser->cursor += 1;
            *value = buffer;
            return true;
        }
        if (ch == '\\') {
            unsigned int unicode_value;

            json_parser->cursor += 1;
            ch = *json_parser->cursor;
            if (ch == '\0') {
                free(buffer);
                return false;
            }
            switch (ch) {
                case '"':
                case '\\':
                case '/':
                    buffer[length++] = ch;
                    break;
                case 'b':
                    buffer[length++] = '\b';
                    break;
                case 'f':
                    buffer[length++] = '\f';
                    break;
                case 'n':
                    buffer[length++] = '\n';
                    break;
                case 'r':
                    buffer[length++] = '\r';
                    break;
                case 't':
                    buffer[length++] = '\t';
                    break;
                case 'u':
                    if (!decode_hex4(json_parser->cursor + 1, &unicode_value)) {
                        free(buffer);
                        return false;
                    }
                    buffer[length++] = unicode_value <= 0x7fu ? (char)unicode_value : '?';
                    json_parser->cursor += 4;
                    break;
                default:
                    free(buffer);
                    return false;
            }
        } else {
            buffer[length++] = ch;
        }
        json_parser->cursor += 1;
    }

    free(buffer);
    return false;
}

static bool parse_json_number(json_parser_t *json_parser, long *value)
{
    char *end_cursor;

    if (json_parser == 0 || value == 0 || json_parser->cursor == 0) {
        return false;
    }
    end_cursor = 0;
    *value = strtol(json_parser->cursor, &end_cursor, 10);
    if (end_cursor == json_parser->cursor) {
        return false;
    }
    json_parser->cursor = end_cursor;
    return true;
}

static bool json_object_has_key(const json_value_t *json_value, const char *key)
{
    size_t index;

    if (json_value == 0 || key == 0 || json_value->type != JSON_VALUE_OBJECT) {
        return false;
    }
    for (index = 0; index < json_value->data.object_value.count; ++index) {
        if (strcmp(json_value->data.object_value.items[index].key, key) == 0) {
            return true;
        }
    }
    return false;
}

static bool parse_json_value(json_parser_t *json_parser, json_value_t *json_value, int depth);

static bool parse_json_object(json_parser_t *json_parser, json_value_t *json_value, int depth)
{
    if (json_parser == 0 || json_value == 0 || *json_parser->cursor != '{') {
        return false;
    }

    json_value->type = JSON_VALUE_OBJECT;
    json_parser->cursor += 1;
    skip_whitespace(json_parser);
    if (*json_parser->cursor == '}') {
        json_parser->cursor += 1;
        return true;
    }

    while (*json_parser->cursor != '\0') {
        char *key;
        json_value_t member_value;
        json_object_member_t *new_items;
        json_value_t *stored_value;
        size_t new_count;

        key = 0;
        json_value_init(&member_value);
        if (!parse_json_string(json_parser, &key)) {
            return false;
        }
        if (json_object_has_key(json_value, key)) {
            free(key);
            return false;
        }

        skip_whitespace(json_parser);
        if (*json_parser->cursor != ':') {
            free(key);
            return false;
        }
        json_parser->cursor += 1;
        skip_whitespace(json_parser);
        if (!parse_json_value(json_parser, &member_value, depth + 1)) {
            free(key);
            return false;
        }

        stored_value = (json_value_t *)malloc(sizeof(*stored_value));
        if (stored_value == 0) {
            free(key);
            json_value_free(&member_value);
            return false;
        }
        *stored_value = member_value;

        new_count = json_value->data.object_value.count + 1;
        new_items = (json_object_member_t *)realloc(json_value->data.object_value.items,
            new_count * sizeof(*new_items));
        if (new_items == 0) {
            free(key);
            json_value_free(stored_value);
            free(stored_value);
            return false;
        }

        json_value->data.object_value.items = new_items;
        json_value->data.object_value.items[new_count - 1].key = key;
        json_value->data.object_value.items[new_count - 1].value = stored_value;
        json_value->data.object_value.count = new_count;

        skip_whitespace(json_parser);
        if (*json_parser->cursor == '}') {
            json_parser->cursor += 1;
            return true;
        }
        if (*json_parser->cursor != ',') {
            return false;
        }
        json_parser->cursor += 1;
        skip_whitespace(json_parser);
    }

    return false;
}

static bool parse_json_array(json_parser_t *json_parser, json_value_t *json_value, int depth)
{
    if (json_parser == 0 || json_value == 0 || *json_parser->cursor != '[') {
        return false;
    }

    json_value->type = JSON_VALUE_ARRAY;
    json_parser->cursor += 1;
    skip_whitespace(json_parser);
    if (*json_parser->cursor == ']') {
        json_parser->cursor += 1;
        return true;
    }

    while (*json_parser->cursor != '\0') {
        json_value_t item_value;
        json_value_t **new_items;
        json_value_t *stored_value;
        size_t new_count;

        json_value_init(&item_value);
        if (!parse_json_value(json_parser, &item_value, depth + 1)) {
            return false;
        }

        stored_value = (json_value_t *)malloc(sizeof(*stored_value));
        if (stored_value == 0) {
            json_value_free(&item_value);
            return false;
        }
        *stored_value = item_value;

        new_count = json_value->data.array_value.count + 1;
        new_items = (json_value_t **)realloc(json_value->data.array_value.items,
            new_count * sizeof(*new_items));
        if (new_items == 0) {
            json_value_free(stored_value);
            free(stored_value);
            return false;
        }

        json_value->data.array_value.items = new_items;
        json_value->data.array_value.items[new_count - 1] = stored_value;
        json_value->data.array_value.count = new_count;

        skip_whitespace(json_parser);
        if (*json_parser->cursor == ']') {
            json_parser->cursor += 1;
            return true;
        }
        if (*json_parser->cursor != ',') {
            return false;
        }
        json_parser->cursor += 1;
        skip_whitespace(json_parser);
    }

    return false;
}

static bool parse_json_value(json_parser_t *json_parser, json_value_t *json_value, int depth)
{
    char *string_value;
    long number_value;

    if (json_parser == 0 || json_value == 0 || depth > JSON_MAX_NESTING_DEPTH) {
        return false;
    }

    skip_whitespace(json_parser);
    if (json_parser->cursor == 0 || *json_parser->cursor == '\0') {
        return false;
    }

    json_value_init(json_value);
    switch (*json_parser->cursor) {
        case '{':
            return parse_json_object(json_parser, json_value, depth);
        case '[':
            return parse_json_array(json_parser, json_value, depth);
        case '"':
            string_value = 0;
            if (!parse_json_string(json_parser, &string_value)) {
                return false;
            }
            json_value->type = JSON_VALUE_STRING;
            json_value->data.string_value = string_value;
            return true;
        case 't':
            if (strncmp(json_parser->cursor, "true", 4) != 0) {
                return false;
            }
            json_value->type = JSON_VALUE_BOOL;
            json_value->data.bool_value = true;
            json_parser->cursor += 4;
            return true;
        case 'f':
            if (strncmp(json_parser->cursor, "false", 5) != 0) {
                return false;
            }
            json_value->type = JSON_VALUE_BOOL;
            json_value->data.bool_value = false;
            json_parser->cursor += 5;
            return true;
        case 'n':
            if (strncmp(json_parser->cursor, "null", 4) != 0) {
                return false;
            }
            json_value->type = JSON_VALUE_NULL;
            json_parser->cursor += 4;
            return true;
        default:
            if (!parse_json_number(json_parser, &number_value)) {
                return false;
            }
            json_value->type = JSON_VALUE_NUMBER;
            json_value->data.number_value = number_value;
            return true;
    }
}

static bool parse_json_document(const char *text, json_value_t *root_value)
{
    json_parser_t json_parser;

    if (text == 0 || root_value == 0) {
        return false;
    }

    json_parser.cursor = text;
    if (!parse_json_value(&json_parser, root_value, 0)) {
        return false;
    }
    skip_whitespace(&json_parser);
    return *json_parser.cursor == '\0';
}

static const json_value_t *json_object_find(const json_value_t *json_value, const char *key)
{
    size_t index;

    if (json_value == 0 || key == 0 || json_value->type != JSON_VALUE_OBJECT) {
        return 0;
    }

    for (index = 0; index < json_value->data.object_value.count; ++index) {
        if (strcmp(json_value->data.object_value.items[index].key, key) == 0) {
            return json_value->data.object_value.items[index].value;
        }
    }
    return 0;
}

static operation_result_t require_member(const json_value_t *json_value,
    const char *key,
    json_value_type_t expected_type,
    const json_value_t **member_value)
{
    const json_value_t *found_value;

    found_value = json_object_find(json_value, key);
    if (found_value == 0 || found_value->type != expected_type) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    *member_value = found_value;
    return operation_result_ok();
}

static operation_result_t optional_member(const json_value_t *json_value,
    const char *key,
    json_value_type_t expected_type,
    const json_value_t **member_value,
    bool *present)
{
    const json_value_t *found_value;

    found_value = json_object_find(json_value, key);
    if (found_value == 0) {
        *member_value = 0;
        *present = false;
        return operation_result_ok();
    }
    if (found_value->type != expected_type) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    *member_value = found_value;
    *present = true;
    return operation_result_ok();
}

static operation_result_t copy_required_string_member(const json_value_t *json_value,
    const char *key,
    char *target,
    size_t target_size)
{
    const json_value_t *member_value;
    size_t length;
    operation_result_t result;

    result = require_member(json_value, key, JSON_VALUE_STRING, &member_value);
    if (!result.ok || target == 0 || target_size == 0) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    length = strlen(member_value->data.string_value);
    if (length >= target_size) {
        length = target_size - 1;
    }
    memcpy(target, member_value->data.string_value, length);
    target[length] = '\0';
    return operation_result_ok();
}

static operation_result_t copy_optional_string_member(const json_value_t *json_value,
    const char *key,
    char *target,
    size_t target_size,
    bool *present)
{
    const json_value_t *member_value;
    size_t length;
    operation_result_t result;

    result = optional_member(json_value, key, JSON_VALUE_STRING, &member_value, present);
    if (!result.ok) {
        return result;
    }
    if (!*present) {
        return operation_result_ok();
    }

    length = strlen(member_value->data.string_value);
    if (length >= target_size) {
        length = target_size - 1;
    }
    memcpy(target, member_value->data.string_value, length);
    target[length] = '\0';
    return operation_result_ok();
}

static operation_result_t read_required_int_member(const json_value_t *json_value,
    const char *key,
    int *value)
{
    const json_value_t *member_value;
    operation_result_t result;

    result = require_member(json_value, key, JSON_VALUE_NUMBER, &member_value);
    if (!result.ok || value == 0) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    if (member_value->data.number_value < INT_MIN || member_value->data.number_value > INT_MAX) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    *value = (int)member_value->data.number_value;
    return operation_result_ok();
}

static operation_result_t read_optional_int_member(const json_value_t *json_value,
    const char *key,
    int *value,
    bool *present)
{
    const json_value_t *member_value;
    operation_result_t result;

    result = optional_member(json_value, key, JSON_VALUE_NUMBER, &member_value, present);
    if (!result.ok) {
        return result;
    }
    if (!*present) {
        return operation_result_ok();
    }
    if (member_value->data.number_value < INT_MIN || member_value->data.number_value > INT_MAX) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    *value = (int)member_value->data.number_value;
    return operation_result_ok();
}

static operation_result_t read_optional_bool_member(const json_value_t *json_value,
    const char *key,
    bool *value,
    bool *present)
{
    const json_value_t *member_value;
    operation_result_t result;

    result = optional_member(json_value, key, JSON_VALUE_BOOL, &member_value, present);
    if (!result.ok) {
        return result;
    }
    if (*present && value != 0) {
        *value = member_value->data.bool_value;
    }
    return operation_result_ok();
}

static bool parse_exception_strategy(const char *text, exception_strategy_t *exception_strategy)
{
    if (strcmp(text, "abort_session") == 0) {
        *exception_strategy = EXCEPTION_STRATEGY_ABORT_SESSION;
        return true;
    }
    if (strcmp(text, "safe_finish") == 0) {
        *exception_strategy = EXCEPTION_STRATEGY_SAFE_FINISH;
        return true;
    }
    return false;
}

static bool parse_segment_kind(const char *text, segment_kind_t *segment_kind)
{
    if (strcmp(text, "roof_brush") == 0) {
        *segment_kind = SEGMENT_KIND_ROOF_BRUSH;
        return true;
    }
    if (strcmp(text, "side_brush") == 0) {
        *segment_kind = SEGMENT_KIND_SIDE_BRUSH;
        return true;
    }
    if (strcmp(text, "ro_water") == 0) {
        *segment_kind = SEGMENT_KIND_RO_WATER;
        return true;
    }
    if (strcmp(text, "dryer") == 0) {
        *segment_kind = SEGMENT_KIND_DRYER;
        return true;
    }
    return false;
}

static bool parse_motion_direction(const char *text, motion_direction_t *motion_direction)
{
    if (strcmp(text, "forward") == 0) {
        *motion_direction = MOTION_DIRECTION_FORWARD;
        return true;
    }
    if (strcmp(text, "reverse") == 0) {
        *motion_direction = MOTION_DIRECTION_REVERSE;
        return true;
    }
    if (strcmp(text, "stop") == 0) {
        *motion_direction = MOTION_DIRECTION_STOP;
        return true;
    }
    return false;
}

static bool parse_motion_target(const char *text, motion_target_reference_t *motion_target_reference)
{
    if (strcmp(text, "head") == 0) {
        *motion_target_reference = MOTION_TARGET_HEAD;
        return true;
    }
    if (strcmp(text, "tail") == 0) {
        *motion_target_reference = MOTION_TARGET_TAIL;
        return true;
    }
    if (strcmp(text, "home") == 0) {
        *motion_target_reference = MOTION_TARGET_HOME;
        return true;
    }
    if (strcmp(text, "relative_distance") == 0) {
        *motion_target_reference = MOTION_TARGET_RELATIVE_DISTANCE;
        return true;
    }
    if (strcmp(text, "absolute_position") == 0) {
        *motion_target_reference = MOTION_TARGET_ABSOLUTE_POSITION;
        return true;
    }
    return false;
}

static bool parse_position_reference(const char *text, position_reference_t *position_reference)
{
    if (strcmp(text, "absolute_mm") == 0) {
        *position_reference = POSITION_REFERENCE_ABSOLUTE_MM;
        return true;
    }
    if (strcmp(text, "distance_to_head_mm") == 0) {
        *position_reference = POSITION_REFERENCE_DISTANCE_TO_HEAD_MM;
        return true;
    }
    if (strcmp(text, "distance_to_tail_mm") == 0) {
        *position_reference = POSITION_REFERENCE_DISTANCE_TO_TAIL_MM;
        return true;
    }
    if (strcmp(text, "head_reached") == 0) {
        *position_reference = POSITION_REFERENCE_HEAD_REACHED;
        return true;
    }
    if (strcmp(text, "tail_reached") == 0) {
        *position_reference = POSITION_REFERENCE_TAIL_REACHED;
        return true;
    }
    if (strcmp(text, "home_reached") == 0) {
        *position_reference = POSITION_REFERENCE_HOME_REACHED;
        return true;
    }
    return false;
}

static bool parse_compare_mode(const char *text, position_compare_mode_t *position_compare_mode)
{
    if (strcmp(text, "true") == 0) {
        *position_compare_mode = POSITION_COMPARE_TRUE;
        return true;
    }
    if (strcmp(text, "greater_equal") == 0) {
        *position_compare_mode = POSITION_COMPARE_GREATER_EQUAL;
        return true;
    }
    if (strcmp(text, "less_equal") == 0) {
        *position_compare_mode = POSITION_COMPARE_LESS_EQUAL;
        return true;
    }
    if (strcmp(text, "range_inclusive") == 0) {
        *position_compare_mode = POSITION_COMPARE_RANGE_INCLUSIVE;
        return true;
    }
    return false;
}

static operation_result_t parse_position_trigger_object(const json_value_t *json_value,
    position_trigger_t *position_trigger)
{
    char text[64];
    bool present;
    operation_result_t result;

    if (json_value == 0 || json_value->type != JSON_VALUE_OBJECT) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    position_trigger_init(position_trigger);
    result = copy_required_string_member(json_value, "reference", text, sizeof(text));
    if (!result.ok || !parse_position_reference(text, &position_trigger->reference)) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_required_string_member(json_value, "compare_mode", text, sizeof(text));
    if (!result.ok || !parse_compare_mode(text, &position_trigger->compare_mode)) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    present = false;
    result = read_optional_int_member(json_value, "value_mm", &position_trigger->value_mm, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_int_member(json_value, "upper_value_mm", &position_trigger->upper_value_mm, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_int_member(json_value, "tolerance_mm", &position_trigger->tolerance_mm, &present);
    if (!result.ok) {
        return result;
    }
    return position_trigger_is_valid(position_trigger)
        ? operation_result_ok()
        : operation_result_fail(ERROR_CODE_PARSE_FAILED);
}

static operation_result_t parse_motion_plan(const json_value_t *segment_value,
    segment_motion_plan_t *segment_motion_plan)
{
    const json_value_t *motion_plan_value;
    char text[64];
    bool present;
    operation_result_t result;

    segment_motion_plan_init(segment_motion_plan);
    result = require_member(segment_value, "motion_plan", JSON_VALUE_OBJECT, &motion_plan_value);
    if (!result.ok) {
        return result;
    }

    result = copy_required_string_member(motion_plan_value, "direction", text, sizeof(text));
    if (!result.ok || !parse_motion_direction(text, &segment_motion_plan->direction)) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_required_string_member(motion_plan_value, "target_reference", text, sizeof(text));
    if (!result.ok || !parse_motion_target(text, &segment_motion_plan->target_reference)) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = copy_optional_string_member(motion_plan_value, "relative_basis", text, sizeof(text), &present);
    if (!result.ok) {
        return result;
    }
    if (present && !parse_position_reference(text, &segment_motion_plan->relative_basis)) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = read_optional_int_member(motion_plan_value, "target_distance_mm",
        &segment_motion_plan->target_distance_mm, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_int_member(motion_plan_value, "target_tolerance_mm",
        &segment_motion_plan->target_tolerance_mm, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(motion_plan_value, "requires_position_feedback",
        &segment_motion_plan->requires_position_feedback, &present);
    if (!result.ok) {
        return result;
    }
    if (!present) {
        segment_motion_plan->requires_position_feedback = true;
    }

    return segment_motion_plan_is_valid(segment_motion_plan)
        ? operation_result_ok()
        : operation_result_fail(ERROR_CODE_PARSE_FAILED);
}

static operation_result_t parse_continuous_controls(const json_value_t *segment_value,
    segment_continuous_controls_t *segment_continuous_controls)
{
    const json_value_t *controls_value;
    bool present;
    operation_result_t result;

    memset(segment_continuous_controls, 0, sizeof(*segment_continuous_controls));
    result = require_member(segment_value, "continuous_controls", JSON_VALUE_OBJECT, &controls_value);
    if (!result.ok) {
        return result;
    }

    result = read_optional_bool_member(controls_value, "roof_brush_follow",
        &segment_continuous_controls->roof_brush_follow, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(controls_value, "side_brush_enabled",
        &segment_continuous_controls->side_brush_enabled, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(controls_value, "ro_water_enabled",
        &segment_continuous_controls->ro_water_enabled, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(controls_value, "dryer_enabled",
        &segment_continuous_controls->dryer_enabled, &present);
    if (!result.ok) {
        return result;
    }
    return operation_result_ok();
}

static operation_result_t parse_conditional_controls(const json_value_t *segment_value,
    wash_segment_t *wash_segment)
{
    const json_value_t *conditional_controls_value;
    size_t index;
    bool present;
    operation_result_t result;

    wash_segment->conditional_control_count = 0;
    result = optional_member(segment_value, "conditional_controls",
        JSON_VALUE_ARRAY, &conditional_controls_value, &present);
    if (!result.ok) {
        return result;
    }
    if (!present) {
        return operation_result_ok();
    }

    for (index = 0; index < conditional_controls_value->data.array_value.count; ++index) {
        const json_value_t *control_value;
        conditional_control_t *conditional_control;
        char text[64];

        control_value = conditional_controls_value->data.array_value.items[index];
        if (control_value == 0 || control_value->type != JSON_VALUE_OBJECT) {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        if (wash_segment->conditional_control_count >= MAX_SEGMENT_CONDITIONAL_CONTROLS) {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        conditional_control = &wash_segment->conditional_controls[wash_segment->conditional_control_count];
        conditional_control_init(conditional_control);
        result = copy_required_string_member(control_value, "control_id",
            conditional_control->control_id, sizeof(conditional_control->control_id));
        if (!result.ok) {
            return result;
        }
        result = copy_required_string_member(control_value, "control_object", text, sizeof(text));
        if (!result.ok) {
            return result;
        }
        if (strcmp(text, "chemical") != 0) {
            return operation_result_fail(ERROR_CODE_UNSUPPORTED);
        }
        conditional_control->kind = CONDITIONAL_CONTROL_CHEMICAL_WINDOW;
        conditional_control->control_object = ACTUATOR_CHEMICAL;
        result = copy_required_string_member(control_value, "basis", text, sizeof(text));
        if (!result.ok || !parse_position_reference(text, &conditional_control->basis)) {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        result = require_member(control_value, "start_trigger", JSON_VALUE_OBJECT, &segment_value);
        if (!result.ok) {
            return result;
        }
        result = parse_position_trigger_object(segment_value, &conditional_control->start_trigger);
        if (!result.ok) {
            return result;
        }
        result = require_member(control_value, "stop_trigger", JSON_VALUE_OBJECT, &segment_value);
        if (!result.ok) {
            return result;
        }
        result = parse_position_trigger_object(segment_value, &conditional_control->stop_trigger);
        if (!result.ok) {
            return result;
        }
        result = read_optional_bool_member(control_value, "active_during_exit",
            &conditional_control->active_during_exit, &present);
        if (!result.ok) {
            return result;
        }
        if (!present) {
            conditional_control->active_during_exit = false;
        }
        if (!conditional_control_is_valid(conditional_control)) {
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        wash_segment->conditional_control_count += 1;
    }

    return operation_result_ok();
}

static operation_result_t parse_completion_condition(const json_value_t *segment_value,
    segment_completion_condition_t *segment_completion_condition)
{
    const json_value_t *completion_value;
    operation_result_t result;

    memset(segment_completion_condition, 0, sizeof(*segment_completion_condition));
    result = require_member(segment_value, "completion_condition", JSON_VALUE_OBJECT, &completion_value);
    if (!result.ok) {
        return result;
    }
    return parse_position_trigger_object(completion_value, &segment_completion_condition->trigger);
}

static operation_result_t parse_exit_actions(const json_value_t *segment_value,
    segment_exit_actions_t *segment_exit_actions)
{
    const json_value_t *exit_actions_value;
    bool present;
    operation_result_t result;

    segment_exit_actions_init(segment_exit_actions);
    result = require_member(segment_value, "exit_actions", JSON_VALUE_OBJECT, &exit_actions_value);
    if (!result.ok) {
        return result;
    }

    result = read_optional_bool_member(exit_actions_value, "stop_roof_brush",
        &segment_exit_actions->stop_roof_brush, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "stop_side_brush",
        &segment_exit_actions->stop_side_brush, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "stop_chemical",
        &segment_exit_actions->stop_chemical, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "close_ro_water",
        &segment_exit_actions->close_ro_water, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "close_dryer",
        &segment_exit_actions->close_dryer, &present);
    if (!result.ok) {
        return result;
    }
    result = read_optional_bool_member(exit_actions_value, "roof_brush_home",
        &segment_exit_actions->roof_brush_home, &present);
    if (!result.ok) {
        return result;
    }
    return operation_result_ok();
}

static void parse_exception_policy(const json_value_t *segment_value,
    segment_exception_policy_t *segment_exception_policy)
{
    const json_value_t *exception_policy_value;
    bool present;
    char text[64];
    operation_result_t result;

    segment_exception_policy_init(segment_exception_policy);
    result = optional_member(segment_value, "exception_policy",
        JSON_VALUE_OBJECT, &exception_policy_value, &present);
    if (!result.ok || !present) {
        return;
    }

    result = copy_optional_string_member(exception_policy_value, "on_position_lost",
        text, sizeof(text), &present);
    if (result.ok && present) {
        (void)parse_exception_strategy(text, &segment_exception_policy->on_position_lost);
    }
    result = copy_optional_string_member(exception_policy_value, "on_follow_lost",
        text, sizeof(text), &present);
    if (result.ok && present) {
        (void)parse_exception_strategy(text, &segment_exception_policy->on_follow_lost);
    }
    result = copy_optional_string_member(exception_policy_value, "on_segment_timeout",
        text, sizeof(text), &present);
    if (result.ok && present) {
        (void)parse_exception_strategy(text, &segment_exception_policy->on_segment_timeout);
    }
    result = copy_optional_string_member(exception_policy_value, "on_exit_timeout",
        text, sizeof(text), &present);
    if (result.ok && present) {
        (void)parse_exception_strategy(text, &segment_exception_policy->on_exit_timeout);
    }
    result = copy_optional_string_member(exception_policy_value, "on_exit_failure",
        text, sizeof(text), &present);
    if (result.ok && present) {
        (void)parse_exception_strategy(text, &segment_exception_policy->on_exit_failure);
    }
}

static operation_result_t parse_segment_object(const json_value_t *segment_value, wash_program_t *wash_program)
{
    char text[64];
    wash_segment_t wash_segment;
    bool present;
    operation_result_t result;

    if (segment_value == 0 || segment_value->type != JSON_VALUE_OBJECT) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    wash_segment_init(&wash_segment, "", "", 0);
    result = copy_required_string_member(segment_value, "segment_id",
        wash_segment.segment_id, sizeof(wash_segment.segment_id));
    if (!result.ok) {
        return result;
    }
    result = copy_required_string_member(segment_value, "segment_name",
        wash_segment.segment_name, sizeof(wash_segment.segment_name));
    if (!result.ok) {
        return result;
    }
    result = read_required_int_member(segment_value, "sequence_no", &wash_segment.sequence_no);
    if (!result.ok) {
        return result;
    }
    result = copy_required_string_member(segment_value, "segment_kind", text, sizeof(text));
    if (!result.ok || !parse_segment_kind(text, &wash_segment.segment_kind)) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    result = parse_motion_plan(segment_value, &wash_segment.motion_plan);
    if (!result.ok) {
        return result;
    }
    result = parse_continuous_controls(segment_value, &wash_segment.continuous_controls);
    if (!result.ok) {
        return result;
    }
    result = parse_conditional_controls(segment_value, &wash_segment);
    if (!result.ok) {
        return result;
    }
    result = parse_completion_condition(segment_value, &wash_segment.completion_condition);
    if (!result.ok) {
        return result;
    }
    result = parse_exit_actions(segment_value, &wash_segment.exit_actions);
    if (!result.ok) {
        return result;
    }
    result = read_optional_int_member(segment_value, "segment_timeout_ms",
        &wash_segment.segment_timeout_ms, &present);
    if (!result.ok) {
        return result;
    }
    if (!present) {
        wash_segment.segment_timeout_ms = wash_program->default_segment_timeout_ms;
    }
    result = read_optional_int_member(segment_value, "exit_timeout_ms",
        &wash_segment.exit_timeout_ms, &present);
    if (!result.ok) {
        return result;
    }
    if (!present) {
        wash_segment.exit_timeout_ms = wash_program->default_exit_timeout_ms;
    }
    parse_exception_policy(segment_value, &wash_segment.exception_policy);
    if (!wash_segment_is_valid(&wash_segment)) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    return wash_program_add_segment(wash_program, &wash_segment) == 0
        ? operation_result_ok()
        : operation_result_fail(ERROR_CODE_PARSE_FAILED);
}

operation_result_t json_program_parser_parse(const char *config_path, wash_program_t *wash_program)
{
    char buffer[16384];
    char program_id[64];
    char program_name[32];
    json_value_t root_value;
    const json_value_t *segments_value;
    bool present;
    size_t index;
    operation_result_t result;

    if (config_path == 0 || wash_program == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!read_file_text(config_path, buffer, sizeof(buffer))) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    json_value_init(&root_value);
    if (!parse_json_document(buffer, &root_value) || root_value.type != JSON_VALUE_OBJECT) {
        json_value_free(&root_value);
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    if (json_object_find(&root_value, "stages") != 0) {
        json_value_free(&root_value);
        return operation_result_fail(ERROR_CODE_UNSUPPORTED);
    }

    result = copy_required_string_member(&root_value, "program_id", program_id, sizeof(program_id));
    if (!result.ok) {
        json_value_free(&root_value);
        return result;
    }
    result = copy_required_string_member(&root_value, "program_name", program_name, sizeof(program_name));
    if (!result.ok) {
        json_value_free(&root_value);
        return result;
    }

    wash_program_init(wash_program, program_id, program_name);
    result = read_optional_int_member(&root_value, "revision", &wash_program->revision, &present);
    if (!result.ok) {
        json_value_free(&root_value);
        return result;
    }
    result = read_optional_bool_member(&root_value, "enabled", &wash_program->enabled, &present);
    if (!result.ok) {
        json_value_free(&root_value);
        return result;
    }
    result = read_optional_int_member(&root_value, "default_segment_timeout_ms",
        &wash_program->default_segment_timeout_ms, &present);
    if (!result.ok) {
        json_value_free(&root_value);
        return result;
    }
    result = read_optional_int_member(&root_value, "default_exit_timeout_ms",
        &wash_program->default_exit_timeout_ms, &present);
    if (!result.ok) {
        json_value_free(&root_value);
        return result;
    }
    result = require_member(&root_value, "segments", JSON_VALUE_ARRAY, &segments_value);
    if (!result.ok) {
        json_value_free(&root_value);
        return result;
    }

    for (index = 0; index < segments_value->data.array_value.count; ++index) {
        result = parse_segment_object(segments_value->data.array_value.items[index], wash_program);
        if (!result.ok) {
            json_value_free(&root_value);
            return result;
        }
    }

    result = program_validation_validate(wash_program);
    json_value_free(&root_value);
    return result;
}
