#ifndef DOMAIN_PORTS_COMMAND_PORT_H
#define DOMAIN_PORTS_COMMAND_PORT_H

#include "application/dto/command_dto.h"

typedef struct command_port_t {
    void *context;
    int (*dispatch)(void *context, const command_dto_t *command);
} command_port_t;

#endif
