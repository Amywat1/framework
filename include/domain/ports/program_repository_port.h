#ifndef DOMAIN_PORTS_PROGRAM_REPOSITORY_PORT_H
#define DOMAIN_PORTS_PROGRAM_REPOSITORY_PORT_H

/**
 * @file program_repository_port.h
 * @brief 声明程序仓储端口，隔离程序加载、保存与冻结校验。
 */

struct wash_program_t;
struct program_snapshot_t;
struct vehicle_type_t;

/**
 * @brief 程序仓储端口。
 *
 * @note 字符串文件路径、目录结构和外部存储细节只能留在适配层实现中。
 */
typedef struct program_repository_port_t
{
    void *context;
    int (*load_program)(void *context, const char *program_id, struct wash_program_t *wash_program);
    int (*save_program)(void *context, const struct wash_program_t *wash_program);
    int (*load_vehicle_type)(void *context, const char *vehicle_type_id, struct vehicle_type_t *vehicle_type);
    int (*validate_program_snapshot)(void *context, const char *program_id, struct program_snapshot_t *program_snapshot,
                                     struct wash_program_t *wash_program);
} program_repository_port_t;

#endif
