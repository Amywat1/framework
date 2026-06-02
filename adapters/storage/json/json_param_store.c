/**
 * @file    json_param_store.c
 * @brief   基于 JSON 的参数存储适配器实现
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    通过 param_store 端口访问本适配器，使上层不感知底层存储细节。
 */

#include "adapters/storage/json/json_param_store.h"
#include "adapters/storage/json/json_param_store_cfg.h"
#include "ports/storage/param_store.h"
#include "common/log.h"
#include "tools/cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static cJSON          *s_root  = NULL;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * ops 实现
 * ------------------------------------------------------------------------- */
static sw_err_t store_load(void)
{
    FILE    *fp;
    long     len;
    char    *buf = NULL;
    sw_err_t ret = SW_ERR_STORAGE;

    pthread_mutex_lock(&s_mutex);

    if (s_root != NULL)
    {
        cJSON_Delete(s_root);
        s_root = NULL;
    }
    s_root = cJSON_CreateObject();

    fp = fopen(PARAM_STORE_JSON_FILE_PATH, "r");
    if (fp == NULL)
    {
        LOG_WARN("json_param_store: file not found (%s), using defaults",
                 PARAM_STORE_JSON_FILE_PATH);
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STORAGE;
    }

    fseek(fp, 0L, SEEK_END);
    len = ftell(fp);
    rewind(fp);

    if (len > 0L)
    {
        buf = (char *)malloc((size_t)len + 1U);
        if (buf != NULL)
        {
            if (fread(buf, 1U, (size_t)len, fp) == (size_t)len)
            {
                buf[len] = '\0';
                cJSON *parsed = cJSON_Parse(buf);
                if (parsed != NULL)
                {
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

    if (ret == SW_OK)
    {
        LOG_INFO("json_param_store: loaded from %s", PARAM_STORE_JSON_FILE_PATH);
    }
    return ret;
}

static sw_err_t store_save(void)
{
    char *str;
    FILE *fp;

    pthread_mutex_lock(&s_mutex);
    str = (s_root != NULL) ? cJSON_PrintUnformatted(s_root) : NULL;
    pthread_mutex_unlock(&s_mutex);

    if (str == NULL)
    {
        return SW_ERR_STORAGE;
    }

    fp = fopen(PARAM_STORE_JSON_FILE_PATH, "w");
    if (fp == NULL)
    {
        free(str);
        LOG_ERROR("json_param_store: cannot write %s", PARAM_STORE_JSON_FILE_PATH);
        return SW_ERR_STORAGE;
    }

    fputs(str, fp);
    fclose(fp);
    free(str);
    LOG_INFO("json_param_store: saved to %s", PARAM_STORE_JSON_FILE_PATH);
    return SW_OK;
}

static sw_err_t store_get(const char *key, char *buf, size_t buf_size)
{
    sw_err_t ret = SW_ERR_PARAM;

    if ((key == NULL) || (buf == NULL) || (buf_size == 0U))
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL)
    {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if (item != NULL)
        {
            if (cJSON_IsString(item) && (item->valuestring != NULL))
            {
                strncpy(buf, item->valuestring, buf_size - 1U);
                buf[buf_size - 1U] = '\0';
                ret = SW_OK;
            }
            else if (cJSON_IsNumber(item))
            {
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
    if ((key == NULL) || (val == NULL))
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL)
    {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if (item != NULL)
        {
            /* 已存在：根据原始类型更新 */
            if (cJSON_IsNumber(item))
            {
                cJSON_SetNumberValue(item, atof(val));
            }
            else
            {
                free(item->valuestring);
                item->valuestring = strdup(val);
            }
        }
        else
        {
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
    param_store_register(&s_ops);
    LOG_INFO("json_param_store: registered");
}
