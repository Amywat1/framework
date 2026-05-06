#ifndef DOMAIN_PORTS_PROGRAM_REPOSITORY_PORT_H
#define DOMAIN_PORTS_PROGRAM_REPOSITORY_PORT_H

struct wash_program_t;
struct program_snapshot_t;
struct vehicle_type_t;

typedef struct program_repository_port_t {
    void *context;
    int (*load_program)(void *context, const char *program_id, struct wash_program_t *wash_program);
    int (*save_program)(void *context, const struct wash_program_t *wash_program);
    int (*load_vehicle_type)(void *context, const char *vehicle_type_id, struct vehicle_type_t *vehicle_type);
    int (*validate_program_snapshot)(void *context, const char *program_id, struct program_snapshot_t *program_snapshot, struct wash_program_t *wash_program);
} program_repository_port_t;

#endif
