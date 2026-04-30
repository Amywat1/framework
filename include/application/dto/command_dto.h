#ifndef APPLICATION_DTO_COMMAND_DTO_H
#define APPLICATION_DTO_COMMAND_DTO_H

#include <stdbool.h>
#include "domain/model/domain_enums.h"

typedef struct {
    command_type_t command_type;
    char program_id[32];
    char operator_id[32];
    char target_id[32];
    char note[128];
    bool vehicle_present_confirmed;
    int duration_ms;
} command_dto_t;

#endif

