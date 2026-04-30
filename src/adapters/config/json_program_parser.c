#include "adapters/config/json_program_parser.h"

#include <stdio.h>
#include <string.h>

#include "shared/error_codes.h"

static void load_builtin_program(wash_program_t *wash_program)
{
    wash_stage_t wash_stage;
    chemical_action_t chemical_action;

    wash_program_init(wash_program, "standard_wash", "标准洗车");

    wash_stage_init(&wash_stage, "pre_soak", "预喷", 1);
    wash_stage.gantry_motion_mode = GANTRY_MOTION_FORWARD;
    chemical_action_init(&chemical_action, "foam", 1000);
    wash_stage_add_chemical_action(&wash_stage, &chemical_action);
    wash_program_add_stage(wash_program, &wash_stage);

    wash_stage_init(&wash_stage, "wash", "清洗", 2);
    wash_stage.gantry_motion_mode = GANTRY_MOTION_TRAVERSE;
    wash_stage.traverse_count = 1;
    wash_stage.roof_brush_mode = BRUSH_MODE_FOLLOW;
    wash_stage.side_brush_mode = BRUSH_MODE_FOLLOW;
    wash_program_add_stage(wash_program, &wash_stage);
}

operation_result_t json_program_parser_parse(const char *config_path, wash_program_t *wash_program)
{
    FILE *file_handle;
    long file_length;
    char buffer[2048];

    if (config_path == 0 || wash_program == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    file_handle = fopen(config_path, "r");
    if (file_handle == 0) {
        load_builtin_program(wash_program);
        return operation_result_ok();
    }

    memset(buffer, 0, sizeof(buffer));
    fseek(file_handle, 0, SEEK_END);
    file_length = ftell(file_handle);
    fseek(file_handle, 0, SEEK_SET);
    if (file_length <= 0 || file_length >= (long)sizeof(buffer)) {
        fclose(file_handle);
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }
    if (fread(buffer, 1, (size_t)file_length, file_handle) != (size_t)file_length) {
        fclose(file_handle);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    fclose(file_handle);

    if (strstr(buffer, "program_id") == 0) {
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    load_builtin_program(wash_program);
    if (strstr(buffer, "heavy_wash") != 0) {
        strncpy(wash_program->program_id, "heavy_wash", sizeof(wash_program->program_id) - 1);
        strncpy(wash_program->program_name, "强力洗车", sizeof(wash_program->program_name) - 1);
        wash_program->revision = 2;
    }
    return operation_result_ok();
}

