#ifndef ADAPTERS_INBOUND_CLI_COMMAND_ADAPTER_H
#define ADAPTERS_INBOUND_CLI_COMMAND_ADAPTER_H

#include "application/coordinators/system_context.h"

int cli_command_adapter_run(system_context_t *system_context, int argc, char **argv);

#endif

