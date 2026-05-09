#include "adapters/outbound/file_program_repository.h"

#include <stdio.h>
#include <string.h>

#include "adapters/config/json_program_parser.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/program_validation.h"
#include "src/application/coordinators/system_context_private.h"

typedef struct file_repository_context_t {
    char config_root[260];
    wash_program_t runtime_program;
    int runtime_revision;
    int runtime_program_available;
} file_repository_context_t;

static file_repository_context_t g_repository_context;

static int load_program_impl(void *context, const char *program_id, wash_program_t *wash_program)
{
    file_repository_context_t *repository_context = (file_repository_context_t *)context;
    char config_path[320];
    operation_result_t result;

    if (context == 0 || program_id == 0 || wash_program == 0) {
        return -1;
    }
    if (repository_context->runtime_program_available != 0
        && strcmp(program_id, repository_context->runtime_program.program_id) == 0) {
        *wash_program = repository_context->runtime_program;
        return 0;
    }

    snprintf(config_path, sizeof(config_path), "%s/programs/%s.json", repository_context->config_root, program_id);
    result = json_program_parser_parse(config_path, wash_program);
    return result.ok ? 0 : -1;
}

static int save_program_impl(void *context, const wash_program_t *wash_program)
{
    (void)context;
    (void)wash_program;
    return -1;
}

static int load_vehicle_type_impl(void *context, const char *vehicle_type_id, vehicle_type_t *vehicle_type)
{
    (void)context;
    vehicle_type_init(vehicle_type, vehicle_type_id, "通用车型");
    return 0;
}

static int validate_program_snapshot_impl(void *context, const char *program_id, program_snapshot_t *program_snapshot, wash_program_t *wash_program)
{
    operation_result_t result;

    if (context == 0 || program_id == 0 || program_snapshot == 0 || wash_program == 0) {
        return -1;
    }
    if (load_program_impl(context, program_id, wash_program) != 0) {
        program_snapshot_capture(program_snapshot,
            program_id,
            0,
            0,
            0,
            PROGRAM_SNAPSHOT_VALIDATION_UNAVAILABLE);
        return 0;
    }
    result = program_validation_validate(wash_program);
    if (!result.ok) {
        program_snapshot_capture(program_snapshot,
            program_id,
            wash_program->revision,
            0,
            wash_program,
            PROGRAM_SNAPSHOT_VALIDATION_INVALID);
        return 0;
    }
    program_snapshot_capture(program_snapshot,
        program_id,
        wash_program->revision,
        0,
        wash_program,
        PROGRAM_SNAPSHOT_VALIDATION_VALID);
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
    system_context->program_repository_port.validate_program_snapshot = validate_program_snapshot_impl;
}

void file_program_repository_set_runtime_program(system_context_t *system_context, const wash_program_t *wash_program, int revision)
{
    file_repository_context_t *repository_context;

    if (system_context == 0 || wash_program == 0) {
        return;
    }
    repository_context = (file_repository_context_t *)system_context->program_repository_port.context;
    if (repository_context == 0) {
        return;
    }
    repository_context->runtime_program = *wash_program;
    repository_context->runtime_program.revision = revision;
    repository_context->runtime_revision = revision;
    repository_context->runtime_program_available = 1;
}
