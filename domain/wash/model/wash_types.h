/**
 * @file    wash_types.h
 * @brief   洗车流程相关领域类型定义
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef DOMAIN_WASH_TYPES_H
#define DOMAIN_WASH_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 洗车模式
 * ------------------------------------------------------------------------- */
typedef enum {
    WASH_MODE_STANDARD = 0, /* 标准洗（预洗 + 泡沫 + 刷洗 + 高压 + 清洗）*/
    WASH_MODE_QUICK,        /* 快洗（刷洗 + 高压）*/
    WASH_MODE_MAX
} wash_mode_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_WASH_TYPES_H */
