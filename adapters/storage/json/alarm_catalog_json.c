/**
 * @file    alarm_catalog_json.c
 * @brief   报警目录 JSON 加载器实现（cJSON → alarm_def_t[]）
 * @author  huwangwei
 * @date    2026-06-26
 */

#include "adapters/storage/json/alarm_catalog_json.h"
#include "tools/cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 访问/转换辅助
 * ------------------------------------------------------------------------- */
static void jfail(char *err, unsigned errsz, const char *fmt, const char *arg)
{
    if ((err != NULL) && (errsz > 0U))
    {
        (void)snprintf(err, errsz, fmt, arg);
    }
}

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    return ((it != NULL) && cJSON_IsString(it)) ? it->valuestring : NULL;
}

static bool juint(const cJSON *o, const char *k, uint32_t *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if ((it == NULL) || !cJSON_IsNumber(it) || (it->valuedouble < 0.0)) { return false; }
    *out = (uint32_t)it->valuedouble;
    return true;
}

/* level 字符串 → 枚举 */
static bool level_from_str(const char *s, alarm_level_t *out)
{
    if (s == NULL) { return false; }
    if      (strcmp(s, "MINOR")    == 0) { *out = ALARM_LEVEL_MINOR;    return true; }
    else if (strcmp(s, "MAJOR")    == 0) { *out = ALARM_LEVEL_MAJOR;    return true; }
    else if (strcmp(s, "CRITICAL") == 0) { *out = ALARM_LEVEL_CRITICAL; return true; }
    return false;
}

/* clear 字符串 → 枚举 */
static bool clear_from_str(const char *s, alarm_clear_t *out)
{
    if (s == NULL) { return false; }
    if      (strcmp(s, "AUTO")    == 0) { *out = ALARM_CLEAR_AUTO_STATIC; return true; }
    else if (strcmp(s, "LATCHED") == 0) { *out = ALARM_CLEAR_LATCHED;     return true; }
    return false;
}

/* -------------------------------------------------------------------------
 * 解析
 * ------------------------------------------------------------------------- */
static sw_err_t parse_catalog(const cJSON *root, alarm_def_t *out, unsigned max,
                              unsigned *out_count, char *err, unsigned errsz)
{
    const cJSON *cat = cJSON_GetObjectItemCaseSensitive(root, "alarm_catalog");
    if (cat == NULL) { jfail(err, errsz, "%s", "缺少顶层 alarm_catalog"); return SW_ERR_PARAM; }

    const char *ver = jstr(cat, "schema_version");
    if (ver == NULL) { jfail(err, errsz, "%s", "缺少 schema_version"); return SW_ERR_PARAM; }
    if (strcmp(ver, "1.0") != 0)
    {
        jfail(err, errsz, "不支持的 schema_version: %s", ver);
        return SW_ERR_PARAM;
    }

    const cJSON *alarms = cJSON_GetObjectItemCaseSensitive(cat, "alarms");
    if ((alarms == NULL) || !cJSON_IsArray(alarms))
    {
        jfail(err, errsz, "%s", "缺少 alarms 数组");
        return SW_ERR_PARAM;
    }
    int n = cJSON_GetArraySize(alarms);
    if (n <= 0) { jfail(err, errsz, "%s", "alarms 为空"); return SW_ERR_PARAM; }
    if ((unsigned)n > max)
    {
        jfail(err, errsz, "%s", "报警条目超过目录容量");
        return SW_ERR_OVERFLOW;
    }

    for (int i = 0; i < n; ++i)
    {
        const cJSON *item = cJSON_GetArrayItem(alarms, i);
        alarm_def_t *d    = &out[i];

        if (!juint(item, "code", &d->code))
        {
            jfail(err, errsz, "%s", "报警缺少合法 code");
            return SW_ERR_PARAM;
        }
        if (!level_from_str(jstr(item, "level"), &d->level))
        {
            if (err != NULL && errsz > 0U)
            {
                (void)snprintf(err, errsz, "无效 level（MINOR/MAJOR/CRITICAL），code=%u", (unsigned)d->code);
            }
            return SW_ERR_PARAM;
        }
        if (!clear_from_str(jstr(item, "clear"), &d->clear))
        {
            if (err != NULL && errsz > 0U)
            {
                (void)snprintf(err, errsz, "无效 clear（AUTO/LATCHED），code=%u", (unsigned)d->code);
            }
            return SW_ERR_PARAM;
        }
        (void)snprintf(d->desc, sizeof(d->desc), "%s", jstr(item, "desc") ? jstr(item, "desc") : "");
    }

    *out_count = (unsigned)n;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 公开入口
 * ------------------------------------------------------------------------- */
sw_err_t alarm_catalog_load_json_file(const char *path,
                                      alarm_def_t *out, unsigned max,
                                      unsigned *out_count,
                                      char *err, unsigned errsz)
{
    if (err != NULL && errsz > 0U) { err[0] = '\0'; }
    if ((path == NULL) || (out == NULL) || (out_count == NULL) || (max == 0U))
    {
        jfail(err, errsz, "%s", "参数非法");
        return SW_ERR_PARAM;
    }
    *out_count = 0U;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) { jfail(err, errsz, "无法打开文件: %s", path); return SW_ERR_STORAGE; }

    (void)fseek(fp, 0L, SEEK_END);
    long sz = ftell(fp);
    (void)fseek(fp, 0L, SEEK_SET);
    if (sz < 0) { jfail(err, errsz, "%s", "读取文件失败"); (void)fclose(fp); return SW_ERR_STORAGE; }

    char *buf = (char *)malloc((size_t)sz + 1U);
    if (buf == NULL) { jfail(err, errsz, "%s", "内存不足"); (void)fclose(fp); return SW_ERR_NOMEM; }
    size_t rd = fread(buf, 1U, (size_t)sz, fp);
    buf[rd] = '\0';
    (void)fclose(fp);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root == NULL) { jfail(err, errsz, "%s", "JSON 解析失败"); return SW_ERR_PARAM; }

    sw_err_t ret = parse_catalog(root, out, max, out_count, err, errsz);
    cJSON_Delete(root);
    return ret;
}
