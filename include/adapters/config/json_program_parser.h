#ifndef ADAPTERS_CONFIG_JSON_PROGRAM_PARSER_H
#define ADAPTERS_CONFIG_JSON_PROGRAM_PARSER_H

#include "domain/model/wash_program.h"
#include "shared/result_types.h"

operation_result_t json_program_parser_parse(const char *config_path, wash_program_t *wash_program);

#endif

