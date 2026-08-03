/**
 * @file    json_param_store.c
 * @brief   基于 JSON 的参数存储适配器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    通过 param_store 端口访问本适配器，使上层不感知底层存储细节。
 */

#include "adapters/outbound/storage/json/json_param_store.h"

#ifndef PARAM_STORE_JSON_FILE_PATH
#define PARAM_STORE_JSON_FILE_PATH ""
#endif
#include "common/log.h"
#include "ports/outbound/storage/param_store.h"
#include "third_party/cJSON/cJSON.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_PARAM_STORE_PATH_MAX 256U

static cJSON          *s_root                                 = NULL;
static pthread_mutex_t s_mutex                                = PTHREAD_MUTEX_INITIALIZER;
static char            s_file_path[JSON_PARAM_STORE_PATH_MAX] = PARAM_STORE_JSON_FILE_PATH;

static sw_err_t get_file_path(char *buf, size_t buf_size)
{
    if ((buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_file_path[0] == '\0') {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }
    strncpy(buf, s_file_path, buf_size - 1U);
    buf[buf_size - 1U] = '\0';
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * ops 实现
 * ------------------------------------------------------------------------- */
static sw_err_t store_load(void)
{
    FILE    *fp;
    long     len;
    char    *buf = NULL;
    char     path[JSON_PARAM_STORE_PATH_MAX];
    sw_err_t ret = SW_ERR_STORAGE;

    if (get_file_path(path, sizeof(path)) != SW_OK) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);

    if (s_root != NULL) {
        cJSON_Delete(s_root);
        s_root = NULL;
    }
    s_root = cJSON_CreateObject();

    fp = fopen(path, "r");
    if (fp == NULL) {
        LOG_WARN("json_param_store: file not found (%s), using defaults", path);
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STORAGE;
    }

    fseek(fp, 0L, SEEK_END);
    len = ftell(fp);
    rewind(fp);

    if (len > 0L) {
        buf = (char *)malloc((size_t)len + 1U);
        if (buf != NULL) {
            if (fread(buf, 1U, (size_t)len, fp) == (size_t)len) {
                buf[len]      = '\0';
                cJSON *parsed = cJSON_Parse(buf);
                if (parsed != NULL) {
                    cJSON_Delete(s_root);
                    s_root = parsed;
                    ret    = SW_OK;
                }
            }
            free(buf);
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&s_mutex);

    if (ret == SW_OK) {
        LOG_INFO("json_param_store: loaded from %s", path);
    }
    return ret;
}

static sw_err_t store_save(void)
{
    char  path[JSON_PARAM_STORE_PATH_MAX];
    char *str;
    FILE *fp;

    if (get_file_path(path, sizeof(path)) != SW_OK) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    str = (s_root != NULL) ? cJSON_PrintUnformatted(s_root) : NULL;
    pthread_mutex_unlock(&s_mutex);

    if (str == NULL) {
        return SW_ERR_STORAGE;
    }

    fp = fopen(path, "w");
    if (fp == NULL) {
        free(str);
        LOG_ERROR("json_param_store: cannot write %s", path);
        return SW_ERR_STORAGE;
    }

    fputs(str, fp);
    fclose(fp);
    free(str);
    LOG_INFO("json_param_store: saved to %s", path);
    return SW_OK;
}

static sw_err_t store_get(const char *key, char *buf, size_t buf_size)
{
    sw_err_t ret = SW_ERR_PARAM;

    if ((key == NULL) || (buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL) {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if (item != NULL) {
            if (cJSON_IsString(item) && (item->valuestring != NULL)) {
                strncpy(buf, item->valuestring, buf_size - 1U);
                buf[buf_size - 1U] = '\0';
                ret                = SW_OK;
            } else if (cJSON_IsNumber(item)) {
                /* 数值型键：转换为字符串返回 */
                snprintf(buf, buf_size, "%g", item->valuedouble);
                ret = SW_OK;
            }
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

static sw_err_t store_set(const char *key, const char *val)
{
    if ((key == NULL) || (val == NULL)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL) {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if (item != NULL) {
            /* 已存在：根据原始类型更新 */
            if (cJSON_IsNumber(item)) {
                cJSON_SetNumberValue(item, atof(val));
            } else {
                free(item->valuestring);
                item->valuestring = strdup(val);
            }
        } else {
            /* 新键：统一以字符串形式存储 */
            cJSON_AddStringToObject(s_root, key, val);
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 注册
 * ------------------------------------------------------------------------- */
static const param_store_ops_t s_ops = {
    .load = store_load,
    .save = store_save,
    .get  = store_get,
    .set  = store_set,
};

void json_param_store_register(void)
{
    /* s_ops 静态定义且必填字段齐全，注册不应失败；失败即为编程错误 */
    if (param_store_register(&s_ops) != SW_OK) {
        LOG_ERROR("json_param_store: register rejected (ops incomplete)");
        return;
    }
    LOG_INFO("json_param_store: registered");
}

sw_err_t json_param_store_configure(const char *path)
{
    size_t len;

    if ((path == NULL) || (path[0] == '\0')) {
        return SW_ERR_PARAM;
    }

    len = strlen(path);
    if (len >= sizeof(s_file_path)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    memcpy(s_file_path, path, len + 1U);
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}
