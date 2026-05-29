#include "adapters/outbound/file_program_repository.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "adapters/config/json_program_parser.h"
#include "application/coordinators/control_context.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/program_validation.h"
#include "domain/model/vehicle_type.h"

typedef struct file_repository_context_t
{
    char config_root[260];
    wash_program_t runtime_program;
    int runtime_revision;
    int runtime_program_available;
} file_repository_context_t;

/* 单实例假设：与 control_context 单实例约束一致，每个进程只维护一个仓储上下文。 */
static file_repository_context_t g_repository_context;

static bool directory_exists(const char *path)
{
    struct stat path_stat;

    if (path == 0 || path[0] == '\0')
    {
        return false;
    }
    if (stat(path, &path_stat) != 0)
    {
        return false;
    }
    return S_ISDIR(path_stat.st_mode);
}

static int load_program_impl(void *context, const char *program_id, wash_program_t *wash_program)
{
    file_repository_context_t *repository_context = (file_repository_context_t *)context;
    char config_path[320];
    operation_result_t result;

    if (context == 0 || program_id == 0 || wash_program == 0)
    {
        return -1;
    }
    if (repository_context->runtime_program_available != 0 &&
        strcmp(program_id, repository_context->runtime_program.program_id) == 0)
    {
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

static int validate_program_snapshot_impl(void *context, const char *program_id, program_snapshot_t *program_snapshot,
                                          wash_program_t *wash_program)
{
    operation_result_t result;

    if (context == 0 || program_id == 0 || program_snapshot == 0 || wash_program == 0)
    {
        return -1;
    }
    if (load_program_impl(context, program_id, wash_program) != 0)
    {
        program_snapshot_capture(program_snapshot, program_id, 0, 0, 0, PROGRAM_SNAPSHOT_VALIDATION_UNAVAILABLE);
        return 0;
    }
    result = program_validation_validate(wash_program);
    if (!result.ok)
    {
        program_snapshot_capture(program_snapshot, program_id, wash_program->revision, 0, wash_program,
                                 PROGRAM_SNAPSHOT_VALIDATION_INVALID);
        return 0;
    }
    program_snapshot_capture(program_snapshot, program_id, wash_program->revision, 0, wash_program,
                             PROGRAM_SNAPSHOT_VALIDATION_VALID);
    return 0;
}

operation_result_t file_program_repository_init(const char *config_root)
{
    program_repository_port_t program_repository_port;
    char programs_root[320];

    if (config_root == 0 || config_root[0] == '\0')
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!directory_exists(config_root))
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    snprintf(programs_root, sizeof(programs_root), "%s/programs", config_root);
    if (!directory_exists(programs_root))
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    memset(&g_repository_context, 0, sizeof(g_repository_context));
    strncpy(g_repository_context.config_root, config_root, sizeof(g_repository_context.config_root) - 1);
    memset(&program_repository_port, 0, sizeof(program_repository_port));
    program_repository_port.context = &g_repository_context;
    program_repository_port.load_program = load_program_impl;
    program_repository_port.save_program = save_program_impl;
    program_repository_port.load_vehicle_type = load_vehicle_type_impl;
    program_repository_port.validate_program_snapshot = validate_program_snapshot_impl;
    control_context_set_program_repository_port(&program_repository_port);
    if (control_context_program_repository_port() == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return operation_result_ok();
}

void file_program_repository_set_runtime_program(const wash_program_t *wash_program, int revision)
{
    file_repository_context_t *repository_context;
    const program_repository_port_t *program_repository_port;

    if (wash_program == 0)
    {
        return;
    }
    program_repository_port = control_context_program_repository_port();
    if (program_repository_port == 0)
    {
        return;
    }
    repository_context = (file_repository_context_t *)program_repository_port->context;
    if (repository_context == 0)
    {
        return;
    }
    repository_context->runtime_program = *wash_program;
    repository_context->runtime_program.revision = revision;
    repository_context->runtime_revision = revision;
    repository_context->runtime_program_available = 1;
}
