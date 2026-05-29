#ifndef DOMAIN_MODEL_WASH_PROGRAM_H
#define DOMAIN_MODEL_WASH_PROGRAM_H

#include <stdbool.h>

#include "domain/model/wash_segment.h"

#define MAX_PROGRAM_SEGMENTS (8)

/**
 * @file wash_program.h
 * @brief 定义工步程序模型。
 */
/**
 * @brief 描述一套完整洗车程序。
 */
typedef struct wash_program_t
{
    /**< 程序 ID。 */
    char program_id[32];
    /**< 程序名称。 */
    char program_name[32];
    /**< 当前是否启用。 */
    bool enabled;
    /**< 默认工步超时，单位毫秒。 */
    int default_segment_timeout_ms;
    /**< 默认退出超时，单位毫秒。 */
    int default_exit_timeout_ms;
    /**< 程序版本号。 */
    int revision;
    /**< 程序包含的工步段集合。 */
    wash_segment_t segments[MAX_PROGRAM_SEGMENTS];
    /**< 当前工步段数量。 */
    int segment_count;
} wash_program_t;

typedef wash_program_t wash_program_v1_t;

/**
 * @brief 初始化洗车程序对象。
 * @param wash_program 程序对象，不能为空。
 * @param program_id 程序 ID，可为空。
 * @param program_name 程序名称，可为空。
 */
void wash_program_init(wash_program_t *wash_program, const char *program_id, const char *program_name);
/**
 * @brief 向程序末尾追加一个工步段。
 * @param wash_program 程序对象，不能为空。
 * @param wash_segment 待追加工步段，不能为空。
 * @return 追加成功返回新段索引，失败返回负值。
 */
int wash_program_add_segment(wash_program_t *wash_program, const wash_segment_t *wash_segment);
/**
 * @brief 读取指定索引的工步段。
 * @param wash_program 程序对象。
 * @param index 工步段索引。
 * @return 索引有效时返回工步段指针，否则返回 `0`。
 */
const wash_segment_t *wash_program_get_segment(const wash_program_t *wash_program, int index);

#endif
