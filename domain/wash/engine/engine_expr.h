/**
 * @file    engine_expr.h
 * @brief   通用控制引擎表达式编译与求值
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    支持算术(+ - * /)、比较(> < >= <= == !=)、逻辑(AND OR NOT)、
 *          三目(?:)、括号、数字字面量、true/false、以及变量引用。变量包括
 *          $param、裸 DI 名、axes.<id>.position/.valid/.speed、
 *          markers.<id>.position/.valid、phase.elapsed_ms、phase.direction。
 *          变量到数值的映射由调用方通过 engine_expr_env_t.resolve 回调提供，
 *          使表达式模块与 IO/引擎状态解耦。
 */

#ifndef DOMAIN_ENGINE_ENGINE_EXPR_H
#define DOMAIN_ENGINE_ENGINE_EXPR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** 编译后的表达式（不透明类型） */
typedef struct engine_expr engine_expr_t;

/**
 * @brief  变量求值环境
 */
typedef struct {
    /**
     * @brief  解析变量名到数值
     * @param  ctx        调用方上下文
     * @param  name       变量原始文本（如 "$x"、"ESTOP"、"axes.gantry.position"）
     * @param  out_value  输出数值（布尔以 0.0/1.0 表示）
     * @return true=解析成功；false=未知变量（求值将判定为失败）
     */
    bool (*resolve)(void *ctx, const char *name, double *out_value);
    void *ctx;
} engine_expr_env_t;

/**
 * @brief  编译表达式文本为可重复求值的 AST
 * @param  text  表达式字符串
 * @return 成功返回表达式句柄；语法错误返回 NULL（可用 engine_expr_last_error 查看原因）
 */
engine_expr_t *engine_expr_compile(const char *text);

/**
 * @brief  释放表达式
 */
void engine_expr_free(engine_expr_t *expr);

/**
 * @brief  求值表达式
 * @param  expr     编译后的表达式
 * @param  env      变量求值环境（resolve 可为空，则任何变量都判定为失败）
 * @param  out_ok   输出求值是否成功（变量解析失败或参数非法时为 false），可为空
 * @return 求值结果（布尔以 1.0/0.0 表示）；失败时返回 0.0
 */
double engine_expr_eval(const engine_expr_t *expr, const engine_expr_env_t *env, bool *out_ok);

/**
 * @brief  按布尔语义求值（非零为真）
 * @param  out_ok  输出求值是否成功，可为空
 */
bool engine_expr_eval_bool(const engine_expr_t *expr, const engine_expr_env_t *env, bool *out_ok);

/**
 * @brief  返回最近一次编译失败的原因描述（用于诊断）
 */
const char *engine_expr_last_error(void);

/**
 * @brief  变量遍历回调
 * @param  name  变量名
 * @param  ctx   调用方上下文
 * @return true=继续遍历；false=提前终止
 */
typedef bool (*engine_expr_var_fn)(const char *name, void *ctx);

/**
 * @brief  遍历表达式引用的全部变量名（去重）
 * @param  expr  编译后的表达式，可为空（无操作）
 * @param  fn    回调，不可为空
 * @param  ctx   回调上下文
 */
void engine_expr_foreach_var(const engine_expr_t *expr, engine_expr_var_fn fn, void *ctx);

/**
 * @brief  深拷贝表达式 AST
 * @param  expr  源表达式
 * @return 成功返回新表达式；失败返回 NULL
 */
engine_expr_t *engine_expr_clone(const engine_expr_t *expr);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_ENGINE_ENGINE_EXPR_H */
