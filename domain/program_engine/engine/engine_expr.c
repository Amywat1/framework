/**
 * @file    engine_expr.c
 * @brief   表达式编译与求值（tokenizer + 递归下降 + AST）
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "domain/program_engine/engine/engine_expr.h"

#include "domain/program_engine/engine/engine_profile.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 具名常量 */
#define EXPR_TOKEN_MAX    256U /* 单个表达式最大 token 数 */
#define EXPR_NAME_MAX     64U  /* 变量名最大长度 */
#define EXPR_ERR_MAX      128U /* 错误描述缓冲 */
#define EXPR_FUNC_ARG_MAX 4U

/* -------------------------------------------------------------------------
 * 词法
 * ------------------------------------------------------------------------- */
typedef enum {
    TK_END = 0,
    TK_NUM,
    TK_VAR,
    TK_STR,
    TK_AND,
    TK_OR,
    TK_NOT,
    TK_PLUS,
    TK_MINUS,
    TK_MUL,
    TK_DIV,
    TK_GT,
    TK_LT,
    TK_GE,
    TK_LE,
    TK_EQ,
    TK_NE,
    TK_LPAREN,
    TK_RPAREN,
    TK_COMMA,
    TK_QUESTION,
    TK_COLON
} tok_type_t;

typedef struct {
    tok_type_t type;
    double     num;
    char       text[EXPR_NAME_MAX];
} token_t;

/* -------------------------------------------------------------------------
 * AST
 * ------------------------------------------------------------------------- */
typedef enum {
    NODE_NUM,
    NODE_STR,
    NODE_VAR,
    NODE_FUNC,
    NODE_UNARY,  /* op: TK_MINUS / TK_NOT */
    NODE_BINARY, /* op: 算术/比较/AND/OR */
    NODE_TERNARY
} node_kind_t;

typedef struct expr_node {
    node_kind_t       kind;
    tok_type_t        op;                  /* NODE_UNARY / NODE_BINARY 使用 */
    double            num;                 /* NODE_NUM */
    char              name[EXPR_NAME_MAX]; /* NODE_VAR */
    unsigned          arg_count;           /* NODE_FUNC */
    struct expr_node *args[EXPR_FUNC_ARG_MAX];
    struct expr_node *a;                   /* 左/条件/操作数 */
    struct expr_node *b;                   /* 右/真值分支 */
    struct expr_node *c;                   /* 三目假值分支 */
} expr_node_t;

struct engine_expr {
    expr_node_t *root;
};

/* 解析器状态 */
typedef struct {
    token_t  toks[EXPR_TOKEN_MAX];
    unsigned count;
    unsigned pos;
    bool     ok;
} parser_t;

static char s_last_error[EXPR_ERR_MAX];

static void set_error(const char *msg)
{
    (void)strncpy(s_last_error, msg, EXPR_ERR_MAX - 1U);
    s_last_error[EXPR_ERR_MAX - 1U] = '\0';
}

const char *engine_expr_last_error(void)
{
    return s_last_error;
}

/* -------------------------------------------------------------------------
 * 词法分析
 * ------------------------------------------------------------------------- */
static bool is_word_start(char c)
{
    return (isalpha((unsigned char)c) != 0) || (c == '_') || (c == '$');
}

static bool is_word_char(char c)
{
    return (isalnum((unsigned char)c) != 0) || (c == '_') || (c == '.');
}

/**
 * @brief  将文本切分为 token 序列
 * @return true=成功；false=词法错误（已设置错误描述）
 */
static bool tokenize(const char *text, parser_t *p)
{
    const char *s = text;

    p->count = 0U;
    p->pos   = 0U;

    while (*s != '\0') {
        if (isspace((unsigned char)*s) != 0) {
            ++s;
            continue;
        }
        if (p->count >= EXPR_TOKEN_MAX) {
            set_error("token 数超出上限");
            return false;
        }

        token_t *t = &p->toks[p->count];
        t->text[0] = '\0';
        t->num     = 0.0;

        if ((*s == '"') || (*s == '\'')) {
            char     quote = *s++;
            unsigned n     = 0U;

            while ((*s != '\0') && (*s != quote) && (n < (EXPR_NAME_MAX - 1U))) {
                t->text[n++] = *s++;
            }
            if (*s != quote) {
                set_error("字符串字面量未闭合或过长");
                return false;
            }
            ++s;
            t->text[n] = '\0';
            t->type    = TK_STR;
            ++p->count;
            continue;
        }

        /* 数字 */
        if ((isdigit((unsigned char)*s) != 0) || ((*s == '.') && (isdigit((unsigned char)s[1]) != 0))) {
            char  *end = NULL;
            double v   = strtod(s, &end);
            if (end == s) {
                set_error("非法数字");
                return false;
            }
            t->type = TK_NUM;
            t->num  = v;
            s       = end;
            ++p->count;
            continue;
        }

        /* 标识符 / 关键字 */
        if (is_word_start(*s)) {
            unsigned n = 0U;
            char     buf[EXPR_NAME_MAX];

            /* 首字符 $ 允许，其后继续 word char */
            buf[n++] = *s++;
            while ((is_word_char(*s)) && (n < (EXPR_NAME_MAX - 1U))) {
                buf[n++] = *s++;
            }
            buf[n] = '\0';

            if (strcmp(buf, "AND") == 0) {
                t->type = TK_AND;
            } else if (strcmp(buf, "OR") == 0) {
                t->type = TK_OR;
            } else if (strcmp(buf, "NOT") == 0) {
                t->type = TK_NOT;
            } else if (strcmp(buf, "true") == 0) {
                t->type = TK_NUM;
                t->num  = 1.0;
            } else if (strcmp(buf, "false") == 0) {
                t->type = TK_NUM;
                t->num  = 0.0;
            } else {
                t->type = TK_VAR;
                (void)strncpy(t->text, buf, EXPR_NAME_MAX - 1U);
                t->text[EXPR_NAME_MAX - 1U] = '\0';
            }
            ++p->count;
            continue;
        }

        /* 运算符与标点 */
        switch (*s) {
        case '+':
            t->type = TK_PLUS;
            ++s;
            break;
        case '-':
            t->type = TK_MINUS;
            ++s;
            break;
        case '*':
            t->type = TK_MUL;
            ++s;
            break;
        case '/':
            t->type = TK_DIV;
            ++s;
            break;
        case '(':
            t->type = TK_LPAREN;
            ++s;
            break;
        case ')':
            t->type = TK_RPAREN;
            ++s;
            break;
        case ',':
            t->type = TK_COMMA;
            ++s;
            break;
        case '?':
            t->type = TK_QUESTION;
            ++s;
            break;
        case ':':
            t->type = TK_COLON;
            ++s;
            break;
        case '>':
            if (s[1] == '=') {
                t->type = TK_GE;
                s += 2;
            } else {
                t->type = TK_GT;
                ++s;
            }
            break;
        case '<':
            if (s[1] == '=') {
                t->type = TK_LE;
                s += 2;
            } else {
                t->type = TK_LT;
                ++s;
            }
            break;
        case '=':
            if (s[1] == '=') {
                t->type = TK_EQ;
                s += 2;
            } else {
                set_error("非法运算符 '='");
                return false;
            }
            break;
        case '!':
            if (s[1] == '=') {
                t->type = TK_NE;
                s += 2;
            } else {
                set_error("非法运算符 '!'");
                return false;
            }
            break;
        default:
            set_error("非法字符");
            return false;
        }
        ++p->count;
    }

    return true;
}

/* -------------------------------------------------------------------------
 * 语法分析（递归下降）
 * ------------------------------------------------------------------------- */
static expr_node_t *parse_expr(parser_t *p);

static const token_t *peek(parser_t *p)
{
    static const token_t end_tok = {TK_END, 0.0, {0}};
    if (p->pos >= p->count) {
        return &end_tok;
    }
    return &p->toks[p->pos];
}

static const token_t *advance(parser_t *p)
{
    const token_t *t = peek(p);
    if (p->pos < p->count) {
        ++p->pos;
    }
    return t;
}

static expr_node_t *node_alloc(node_kind_t kind)
{
    expr_node_t *n = (expr_node_t *)calloc(1U, sizeof(expr_node_t));
    if (n == NULL) {
        return NULL;
    }
    n->kind = kind;
    return n;
}

static void node_free(expr_node_t *n)
{
    if (n == NULL) {
        return;
    }
    for (unsigned i = 0U; i < n->arg_count; ++i) {
        node_free(n->args[i]);
    }
    node_free(n->a);
    node_free(n->b);
    node_free(n->c);
    free(n);
}

static void parse_fail(parser_t *p, const char *msg)
{
    p->ok = false;
    set_error(msg);
}

static expr_node_t *parse_function_call(parser_t *p, const token_t *name_tok)
{
    expr_node_t *n = node_alloc(NODE_FUNC);
    if (n == NULL) {
        parse_fail(p, "内存不足");
        return NULL;
    }

    (void)strncpy(n->name, name_tok->text, EXPR_NAME_MAX - 1U);
    n->name[EXPR_NAME_MAX - 1U] = '\0';
    (void)advance(p); /* '(' */

    if (peek(p)->type == TK_RPAREN) {
        (void)advance(p);
        return n;
    }

    while (p->ok) {
        if (n->arg_count >= EXPR_FUNC_ARG_MAX) {
            parse_fail(p, "函数参数过多");
            node_free(n);
            return NULL;
        }

        n->args[n->arg_count] = parse_expr(p);
        if (!p->ok) {
            node_free(n);
            return NULL;
        }
        ++n->arg_count;

        if (peek(p)->type == TK_RPAREN) {
            (void)advance(p);
            return n;
        }
        if (peek(p)->type != TK_COMMA) {
            parse_fail(p, "函数参数缺少逗号或右括号");
            node_free(n);
            return NULL;
        }
        (void)advance(p);
    }

    node_free(n);
    return NULL;
}

/* primary := NUM | STR | VAR | function_call | '(' expr ')' */
static expr_node_t *parse_primary(parser_t *p)
{
    const token_t *t = peek(p);

    if (t->type == TK_NUM) {
        (void)advance(p);
        expr_node_t *n = node_alloc(NODE_NUM);
        if (n == NULL) {
            parse_fail(p, "内存不足");
            return NULL;
        }
        n->num = t->num;
        return n;
    }
    if (t->type == TK_STR) {
        (void)advance(p);
        expr_node_t *n = node_alloc(NODE_STR);
        if (n == NULL) {
            parse_fail(p, "内存不足");
            return NULL;
        }
        (void)strncpy(n->name, t->text, EXPR_NAME_MAX - 1U);
        n->name[EXPR_NAME_MAX - 1U] = '\0';
        return n;
    }
    if (t->type == TK_VAR) {
        (void)advance(p);
        if (peek(p)->type == TK_LPAREN) {
            return parse_function_call(p, t);
        }
        expr_node_t *n = node_alloc(NODE_VAR);
        if (n == NULL) {
            parse_fail(p, "内存不足");
            return NULL;
        }
        (void)strncpy(n->name, t->text, EXPR_NAME_MAX - 1U);
        n->name[EXPR_NAME_MAX - 1U] = '\0';
        return n;
    }
    if (t->type == TK_LPAREN) {
        (void)advance(p);
        expr_node_t *n = parse_expr(p);
        if (!p->ok) {
            return n;
        }
        if (peek(p)->type != TK_RPAREN) {
            parse_fail(p, "缺少右括号");
            node_free(n);
            return NULL;
        }
        (void)advance(p);
        return n;
    }

    parse_fail(p, "期望操作数");
    return NULL;
}

/* unary := ('-'|'NOT') unary | primary */
static expr_node_t *parse_unary(parser_t *p)
{
    const token_t *t = peek(p);
    if ((t->type == TK_MINUS) || (t->type == TK_NOT)) {
        tok_type_t op = t->type;
        (void)advance(p);
        expr_node_t *operand = parse_unary(p);
        if (!p->ok) {
            return operand;
        }
        expr_node_t *n = node_alloc(NODE_UNARY);
        if (n == NULL) {
            parse_fail(p, "内存不足");
            node_free(operand);
            return NULL;
        }
        n->op = op;
        n->a  = operand;
        return n;
    }
    return parse_primary(p);
}

/* 通用二元层构造 */
static expr_node_t *make_binary(parser_t *p, tok_type_t op, expr_node_t *a, expr_node_t *b)
{
    expr_node_t *n = node_alloc(NODE_BINARY);
    if (n == NULL) {
        parse_fail(p, "内存不足");
        node_free(a);
        node_free(b);
        return NULL;
    }
    n->op = op;
    n->a  = a;
    n->b  = b;
    return n;
}

/* term := unary (('*'|'/') unary)* */
static expr_node_t *parse_term(parser_t *p)
{
    expr_node_t *a = parse_unary(p);
    while (p->ok) {
        tok_type_t op = peek(p)->type;
        if ((op != TK_MUL) && (op != TK_DIV)) {
            break;
        }
        (void)advance(p);
        expr_node_t *b = parse_unary(p);
        if (!p->ok) {
            node_free(a);
            return b;
        }
        a = make_binary(p, op, a, b);
    }
    return a;
}

/* additive := term (('+'|'-') term)* */
static expr_node_t *parse_additive(parser_t *p)
{
    expr_node_t *a = parse_term(p);
    while (p->ok) {
        tok_type_t op = peek(p)->type;
        if ((op != TK_PLUS) && (op != TK_MINUS)) {
            break;
        }
        (void)advance(p);
        expr_node_t *b = parse_term(p);
        if (!p->ok) {
            node_free(a);
            return b;
        }
        a = make_binary(p, op, a, b);
    }
    return a;
}

/* comparison := additive (relop additive)* */
static expr_node_t *parse_comparison(parser_t *p)
{
    expr_node_t *a = parse_additive(p);
    while (p->ok) {
        tok_type_t op = peek(p)->type;
        if ((op != TK_GT) && (op != TK_LT) && (op != TK_GE) && (op != TK_LE) && (op != TK_EQ) && (op != TK_NE)) {
            break;
        }
        (void)advance(p);
        expr_node_t *b = parse_additive(p);
        if (!p->ok) {
            node_free(a);
            return b;
        }
        a = make_binary(p, op, a, b);
    }
    return a;
}

/* logic_not := 'NOT' logic_not | comparison
 * 说明：一元 NOT 已在 parse_unary 处理，这里保留比较层入口即可。 */
static expr_node_t *parse_logic_and(parser_t *p)
{
    expr_node_t *a = parse_comparison(p);
    while (p->ok && (peek(p)->type == TK_AND)) {
        (void)advance(p);
        expr_node_t *b = parse_comparison(p);
        if (!p->ok) {
            node_free(a);
            return b;
        }
        a = make_binary(p, TK_AND, a, b);
    }
    return a;
}

static expr_node_t *parse_logic_or(parser_t *p)
{
    expr_node_t *a = parse_logic_and(p);
    while (p->ok && (peek(p)->type == TK_OR)) {
        (void)advance(p);
        expr_node_t *b = parse_logic_and(p);
        if (!p->ok) {
            node_free(a);
            return b;
        }
        a = make_binary(p, TK_OR, a, b);
    }
    return a;
}

/* expr := logic_or ('?' expr ':' expr)? */
static expr_node_t *parse_expr(parser_t *p)
{
    expr_node_t *cond = parse_logic_or(p);
    if (!p->ok) {
        return cond;
    }
    if (peek(p)->type == TK_QUESTION) {
        (void)advance(p);
        expr_node_t *t_branch = parse_expr(p);
        if (!p->ok) {
            node_free(cond);
            return t_branch;
        }
        if (peek(p)->type != TK_COLON) {
            parse_fail(p, "三目缺少 ':'");
            node_free(cond);
            node_free(t_branch);
            return NULL;
        }
        (void)advance(p);
        expr_node_t *f_branch = parse_expr(p);
        if (!p->ok) {
            node_free(cond);
            node_free(t_branch);
            return f_branch;
        }

        expr_node_t *n = node_alloc(NODE_TERNARY);
        if (n == NULL) {
            parse_fail(p, "内存不足");
            node_free(cond);
            node_free(t_branch);
            node_free(f_branch);
            return NULL;
        }
        n->a = cond;
        n->b = t_branch;
        n->c = f_branch;
        return n;
    }
    return cond;
}

/* -------------------------------------------------------------------------
 * 编译入口
 * ------------------------------------------------------------------------- */
engine_expr_t *engine_expr_compile(const char *text)
{
    if (text == NULL) {
        set_error("空表达式");
        return NULL;
    }

    parser_t *p = (parser_t *)calloc(1U, sizeof(parser_t));
    if (p == NULL) {
        set_error("内存不足");
        return NULL;
    }

    if (!tokenize(text, p)) {
        free(p);
        return NULL;
    }
    if (p->count == 0U) {
        set_error("空表达式");
        free(p);
        return NULL;
    }

    p->ok             = true;
    p->pos            = 0U;
    expr_node_t *root = parse_expr(p);

    if (p->ok && (p->pos != p->count)) {
        parse_fail(p, "表达式有多余 token");
    }
    if (!p->ok) {
        node_free(root);
        free(p);
        return NULL;
    }

    engine_expr_t *e = (engine_expr_t *)calloc(1U, sizeof(engine_expr_t));
    if (e == NULL) {
        set_error("内存不足");
        node_free(root);
        free(p);
        return NULL;
    }
    e->root = root;
    free(p);
    return e;
}

void engine_expr_free(engine_expr_t *expr)
{
    if (expr == NULL) {
        return;
    }
    node_free(expr->root);
    free(expr);
}

/* -------------------------------------------------------------------------
 * AST 克隆与变量遍历
 * ------------------------------------------------------------------------- */
static expr_node_t *node_clone(const expr_node_t *n)
{
    if (n == NULL) {
        return NULL;
    }

    expr_node_t *c = (expr_node_t *)calloc(1U, sizeof(expr_node_t));
    if (c == NULL) {
        return NULL;
    }

    c->kind = n->kind;
    c->op   = n->op;
    c->num  = n->num;
    (void)strncpy(c->name, n->name, EXPR_NAME_MAX - 1U);
    c->name[EXPR_NAME_MAX - 1U] = '\0';
    c->arg_count                = n->arg_count;

    for (unsigned i = 0U; i < n->arg_count; ++i) {
        c->args[i] = node_clone(n->args[i]);
        if ((n->args[i] != NULL) && (c->args[i] == NULL)) {
            node_free(c);
            return NULL;
        }
    }

    c->a = node_clone(n->a);
    if ((n->a != NULL) && (c->a == NULL)) {
        node_free(c);
        return NULL;
    }
    c->b = node_clone(n->b);
    if ((n->b != NULL) && (c->b == NULL)) {
        node_free(c);
        return NULL;
    }
    c->c = node_clone(n->c);
    if ((n->c != NULL) && (c->c == NULL)) {
        node_free(c);
        return NULL;
    }
    return c;
}

engine_expr_t *engine_expr_clone(const engine_expr_t *expr)
{
    if ((expr == NULL) || (expr->root == NULL)) {
        return NULL;
    }

    engine_expr_t *copy = (engine_expr_t *)calloc(1U, sizeof(engine_expr_t));
    if (copy == NULL) {
        return NULL;
    }

    copy->root = node_clone(expr->root);
    if (copy->root == NULL) {
        free(copy);
        return NULL;
    }
    return copy;
}

static void foreach_node_vars(const expr_node_t *n,
                              engine_expr_var_fn fn,
                              void              *ctx,
                              char               seen[][EXPR_NAME_MAX],
                              unsigned          *seen_count)
{
    if ((n == NULL) || (fn == NULL)) {
        return;
    }

    if (n->kind == NODE_VAR) {
        unsigned i;
        for (i = 0U; i < *seen_count; ++i) {
            if (strcmp(seen[i], n->name) == 0) {
                return;
            }
        }
        if (*seen_count < EXPR_TOKEN_MAX) {
            (void)strncpy(seen[*seen_count], n->name, EXPR_NAME_MAX - 1U);
            seen[*seen_count][EXPR_NAME_MAX - 1U] = '\0';
            ++(*seen_count);
        }
        if (!fn(n->name, ctx)) {
            return;
        }
        return;
    }

    for (unsigned i = 0U; i < n->arg_count; ++i) {
        foreach_node_vars(n->args[i], fn, ctx, seen, seen_count);
    }
    foreach_node_vars(n->a, fn, ctx, seen, seen_count);
    foreach_node_vars(n->b, fn, ctx, seen, seen_count);
    foreach_node_vars(n->c, fn, ctx, seen, seen_count);
}

void engine_expr_foreach_var(const engine_expr_t *expr, engine_expr_var_fn fn, void *ctx)
{
    char     seen[EXPR_TOKEN_MAX][EXPR_NAME_MAX];
    unsigned seen_count = 0U;

    if ((expr == NULL) || (expr->root == NULL) || (fn == NULL)) {
        return;
    }
    foreach_node_vars(expr->root, fn, ctx, seen, &seen_count);
}

/* -------------------------------------------------------------------------
 * 求值
 * ------------------------------------------------------------------------- */
static double eval_node(const expr_node_t *n, const engine_expr_env_t *env, bool *ok)
{
    if (!(*ok) || (n == NULL)) {
        return 0.0;
    }

    switch (n->kind) {
    case NODE_NUM:
        return n->num;

    case NODE_STR:
        *ok = false;
        return 0.0;

    case NODE_VAR: {
        double v = 0.0;
        if ((env == NULL) || (env->resolve == NULL) || (!env->resolve(env->ctx, n->name, &v))) {
            *ok = false;
            return 0.0;
        }
        return v;
    }

    case NODE_FUNC:
        if (strcmp(n->name, "body_contains") == 0) {
            double pos;
            double a;
            double b;
            double lo;
            double hi;
            if (n->arg_count != 3U) {
                *ok = false;
                return 0.0;
            }
            pos = eval_node(n->args[0], env, ok);
            a   = eval_node(n->args[1], env, ok);
            b   = eval_node(n->args[2], env, ok);
            if (!(*ok)) {
                return 0.0;
            }
            lo = (a < b) ? a : b;
            hi = (a < b) ? b : a;
            return ((pos >= lo) && (pos <= hi)) ? 1.0 : 0.0;
        }
        if (strcmp(n->name, "body_covers") == 0) {
            double fixed;
            double front;
            double rear;
            double lo;
            double hi;
            if (n->arg_count != 3U) {
                *ok = false;
                return 0.0;
            }
            fixed = eval_node(n->args[0], env, ok);
            front = eval_node(n->args[1], env, ok);
            rear  = eval_node(n->args[2], env, ok);
            if (!(*ok)) {
                return 0.0;
            }
            lo = (front < rear) ? front : rear;
            hi = (front < rear) ? rear : front;
            return ((fixed >= lo) && (fixed <= hi)) ? 1.0 : 0.0;
        }
        if (strcmp(n->name, "profile.height_at") == 0) {
            double pos;
            double def;
            if (n->arg_count != 2U) {
                *ok = false;
                return 0.0;
            }
            pos = eval_node(n->args[0], env, ok);
            def = eval_node(n->args[1], env, ok);
            if (!(*ok)) {
                return 0.0;
            }
            return engine_profile_height_at(pos, def);
        }
        if (strcmp(n->name, "profile.in_zone") == 0) {
            double pos;
            double def;
            if ((n->arg_count != 3U) || (n->args[0] == NULL) || (n->args[0]->kind != NODE_STR)) {
                *ok = false;
                return 0.0;
            }
            pos = eval_node(n->args[1], env, ok);
            def = eval_node(n->args[2], env, ok);
            if (!(*ok)) {
                return 0.0;
            }
            return engine_profile_in_zone(n->args[0]->name, pos, def != 0.0) ? 1.0 : 0.0;
        }

        *ok = false;
        return 0.0;

    case NODE_UNARY: {
        double a = eval_node(n->a, env, ok);
        if (n->op == TK_MINUS) {
            return -a;
        }
        /* TK_NOT */
        return (a != 0.0) ? 0.0 : 1.0;
    }

    case NODE_BINARY: {
        /* 逻辑运算支持短路 */
        if (n->op == TK_AND) {
            double a = eval_node(n->a, env, ok);
            if (!(*ok)) {
                return 0.0;
            }
            if (a == 0.0) {
                return 0.0;
            }
            double b = eval_node(n->b, env, ok);
            return (b != 0.0) ? 1.0 : 0.0;
        }
        if (n->op == TK_OR) {
            double a = eval_node(n->a, env, ok);
            if (!(*ok)) {
                return 0.0;
            }
            if (a != 0.0) {
                return 1.0;
            }
            double b = eval_node(n->b, env, ok);
            return (b != 0.0) ? 1.0 : 0.0;
        }

        double a = eval_node(n->a, env, ok);
        double b = eval_node(n->b, env, ok);
        if (!(*ok)) {
            return 0.0;
        }

        switch (n->op) {
        case TK_PLUS:
            return a + b;
        case TK_MINUS:
            return a - b;
        case TK_MUL:
            return a * b;
        case TK_DIV:
            return (b != 0.0) ? (a / b) : 0.0;
        case TK_GT:
            return (a > b) ? 1.0 : 0.0;
        case TK_LT:
            return (a < b) ? 1.0 : 0.0;
        case TK_GE:
            return (a >= b) ? 1.0 : 0.0;
        case TK_LE:
            return (a <= b) ? 1.0 : 0.0;
        case TK_EQ:
            return (a == b) ? 1.0 : 0.0;
        case TK_NE:
            return (a != b) ? 1.0 : 0.0;
        default:
            *ok = false;
            return 0.0;
        }
    }

    case NODE_TERNARY: {
        double c = eval_node(n->a, env, ok);
        if (!(*ok)) {
            return 0.0;
        }
        return (c != 0.0) ? eval_node(n->b, env, ok) : eval_node(n->c, env, ok);
    }

    default:
        *ok = false;
        return 0.0;
    }
}

double engine_expr_eval(const engine_expr_t *expr, const engine_expr_env_t *env, bool *out_ok)
{
    bool   ok = true;
    double v;

    if ((expr == NULL) || (expr->root == NULL)) {
        if (out_ok != NULL) {
            *out_ok = false;
        }
        return 0.0;
    }
    v = eval_node(expr->root, env, &ok);
    if (out_ok != NULL) {
        *out_ok = ok;
    }
    return ok ? v : 0.0;
}

bool engine_expr_eval_bool(const engine_expr_t *expr, const engine_expr_env_t *env, bool *out_ok)
{
    bool   ok = true;
    double v  = engine_expr_eval(expr, env, &ok);
    if (out_ok != NULL) {
        *out_ok = ok;
    }
    return ok && (v != 0.0);
}
