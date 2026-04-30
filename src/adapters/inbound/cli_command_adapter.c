#include "adapters/inbound/cli_command_adapter.h"

#include <stdio.h>
#include <string.h>

#include "application/dto/command_dto.h"
#include "application/use_cases/start_wash_cycle.h"
#include "application/use_cases/update_program_config.h"
#include "shared/error_codes.h"

int cli_command_adapter_run(system_context_t *system_context, int argc, char **argv)
{
    command_dto_t command_dto;
    operation_result_t result;

    if (argc < 2) {
        return ERROR_CODE_INVALID_ARGUMENT;
    }

    memset(&command_dto, 0, sizeof(command_dto));
    if (strcmp(argv[1], "start") == 0) {
        command_dto.command_type = COMMAND_TYPE_START;
        strncpy(command_dto.program_id, "standard_wash", sizeof(command_dto.program_id) - 1);
        if (argc >= 4 && strcmp(argv[2], "--program") == 0) {
            strncpy(command_dto.program_id, argv[3], sizeof(command_dto.program_id) - 1);
        }
        result = start_wash_cycle_execute(system_context, &command_dto);
        return result.error_code;
    }
    if (strcmp(argv[1], "update-program") == 0 && argc >= 3) {
        return update_program_config_execute(system_context, argv[2]).error_code;
    }
    printf("unsupported command\n");
    return ERROR_CODE_UNSUPPORTED;
}

