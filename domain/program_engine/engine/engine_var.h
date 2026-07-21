/**
 * @file    engine_var.h
 * @brief   引擎表达式项目变量注入（非 DI）
 * @author  HUWANGWEI
 * @date    2026-07-21
 *
 * @note    供方案表达式引用机型语义量（如 car_tail.active），不进入 DI catalog。
 */

#ifndef DOMAIN_PROGRAM_ENGINE_VAR_H
#define DOMAIN_PROGRAM_ENGINE_VAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief  项目变量提供者
 */
typedef struct {
    /**
     * @brief  解析变量名到数值
     * @param  name       变量名（如 "car_tail.active"）
     * @param  out_value  输出（布尔以 0.0/1.0）
     * @return true=已处理；false=非本提供者变量
     */
    bool (*resolve)(const char *name, double *out_value);

    /** 允许出现在方案中的变量名表（供 validate） */
    const char *const *names;
    unsigned           name_count;
} engine_var_provider_t;

/**
 * @brief  注册项目变量提供者；传 NULL 清除
 */
void engine_var_register(const engine_var_provider_t *provider);

/**
 * @brief  获取已注册提供者；未注册返回 NULL
 */
const engine_var_provider_t *engine_var_get(void);

/**
 * @brief  名称是否在提供者目录中
 */
bool engine_var_name_known(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROGRAM_ENGINE_VAR_H */
