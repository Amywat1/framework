#include "adapters/outbound/file_program_repository.h"

#include <stdio.h>
#include <string.h>

#include "adapters/config/json_program_parser.h"

typedef struct file_repository_context_t {
    char config_root[260];
} file_repository_context_t;

static file_repository_context_t g_repository_context;

static void create_default_program(wash_program_t *wash_program)
{
    wash_stage_t wash_stage;
    chemical_action_t chemical_action;

    wash_program_init(wash_program, "standard_wash", "标准洗车");

    wash_stage_init(&wash_stage, "pre_soak", "预喷", 1);
    wash_stage.gantry_motion_mode = GANTRY_MOTION_FORWARD;
    wash_stage.roof_brush_mode = BRUSH_MODE_DISABLED;
    wash_stage.side_brush_mode = BRUSH_MODE_DISABLED;
    chemical_action_init(&chemical_action, "foam", 1000);
    wash_stage_add_chemical_action(&wash_stage, &chemical_action);
    wash_program_add_stage(wash_program, &wash_stage);

    wash_stage_init(&wash_stage, "brush", "刷洗", 2);
    wash_stage.gantry_motion_mode = GANTRY_MOTION_TRAVERSE;
    wash_stage.traverse_count = 1;
    wash_stage.roof_brush_mode = BRUSH_MODE_FOLLOW;
    wash_stage.side_brush_mode = BRUSH_MODE_FOLLOW;
    wash_program_add_stage(wash_program, &wash_stage);
}

static int load_program_impl(void *context, const char *program_id, wash_program_t *wash_program)
{
    char config_path[320];
    operation_result_t result;
    file_repository_context_t *repository_context = (file_repository_context_t *)context;

    snprintf(config_path, sizeof(config_path), "%s/programs/%s.json", repository_context->config_root, program_id);
    result = json_program_parser_parse(config_path, wash_program);
    if (!result.ok) {
        create_default_program(wash_program);
    }
    return 0;
}

static int save_program_impl(void *context, const wash_program_t *wash_program)
{
    char config_path[320];
    FILE *file_handle;
    file_repository_context_t *repository_context = (file_repository_context_t *)context;

    snprintf(config_path, sizeof(config_path), "%s/programs/%s.json", repository_context->config_root, wash_program->program_id);
    file_handle = fopen(config_path, "w");
    if (file_handle == 0) {
        return -1;
    }
    fprintf(file_handle, "{\n  \"program_id\": \"%s\",\n  \"program_name\": \"%s\",\n  \"revision\": %d\n}\n",
        wash_program->program_id,
        wash_program->program_name,
        wash_program->revision);
    fclose(file_handle);
    return 0;
}

static int load_vehicle_type_impl(void *context, const char *vehicle_type_id, vehicle_type_t *vehicle_type)
{
    (void)context;
    vehicle_type_init(vehicle_type, vehicle_type_id, "通用车型");
    return 0;
}

void file_program_repository_init(system_context_t *system_context, const char *config_root)
{
    memset(&g_repository_context, 0, sizeof(g_repository_context));
    strncpy(g_repository_context.config_root, config_root, sizeof(g_repository_context.config_root) - 1);

    system_context->program_repository_port.context = &g_repository_context;
    system_context->program_repository_port.load_program = load_program_impl;
    system_context->program_repository_port.save_program = save_program_impl;
    system_context->program_repository_port.load_vehicle_type = load_vehicle_type_impl;
}

