/**
 * @file    svc_param.c
 * @brief   运行时参数管理实现（cJSON 持久化）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    Phase 6 TODO: 底层 cJSON 文件读写改为调用 param_store_ops 接口，
 *          当前保持与原 service/svc_param.c 相同的实现，仅更新头文件路径。
 */

#include "service/svc_param/svc_param.h"
#include "common/log.h"
#include "tools/cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static cJSON          *s_root  = NULL;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 内部：从文件加载 JSON
 * ------------------------------------------------------------------------- */
static sw_err_t load_from_file(void)
{
    FILE    *fp;
    long     len;
    char    *buf = NULL;
    sw_err_t ret = SW_ERR_STORAGE;

    fp = fopen(SVC_PARAM_FILE_PATH, "r");
    if (fp == NULL)
    {
        LOG_WARN("svc_param: param file not found, using defaults");
        return SW_ERR_STORAGE;
    }

    fseek(fp, 0L, SEEK_END);
    len = ftell(fp);
    rewind(fp);

    if (len <= 0L)
    {
        fclose(fp);
        return SW_ERR_STORAGE;
    }

    buf = (char *)malloc((size_t)len + 1U);
    if (buf == NULL)
    {
        fclose(fp);
        return SW_ERR_NOMEM;
    }

    if (fread(buf, 1U, (size_t)len, fp) == (size_t)len)
    {
        buf[len] = '\0';
        s_root = cJSON_Parse(buf);
        if (s_root != NULL)
        {
            ret = SW_OK;
        }
    }

    free(buf);
    fclose(fp);
    return ret;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t svc_param_init(void)
{
    sw_err_t ret;

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL)
    {
        cJSON_Delete(s_root);
    }
    s_root = cJSON_CreateObject();
    ret    = load_from_file();
    pthread_mutex_unlock(&s_mutex);

    if (ret == SW_OK)
    {
        LOG_INFO("svc_param: loaded from %s", SVC_PARAM_FILE_PATH);
    }
    else
    {
        LOG_WARN("svc_param: using defaults (no persistent file)");
    }
    return ret;
}

int svc_param_get_int(const char *key, int default_val)
{
    int val = default_val;

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL)
    {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if ((item != NULL) && cJSON_IsNumber(item))
        {
            val = (int)item->valuedouble;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return val;
}

sw_err_t svc_param_get_str(const char *key, char *buf, int buf_size,
                            const char *default_val)
{
    bool found = false;

    if ((buf == NULL) || (buf_size <= 0))
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL)
    {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if ((item != NULL) && cJSON_IsString(item) && (item->valuestring != NULL))
        {
            strncpy(buf, item->valuestring, (size_t)(buf_size - 1));
            buf[buf_size - 1] = '\0';
            found = true;
        }
    }
    pthread_mutex_unlock(&s_mutex);

    if (!found)
    {
        strncpy(buf, (default_val != NULL) ? default_val : "",
                (size_t)(buf_size - 1));
        buf[buf_size - 1] = '\0';
    }
    return SW_OK;
}

sw_err_t svc_param_set_int(const char *key, int val)
{
    if (key == NULL)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL)
    {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if (item != NULL)
        {
            cJSON_SetNumberValue(item, (double)val);
        }
        else
        {
            cJSON_AddNumberToObject(s_root, key, (double)val);
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t svc_param_set_str(const char *key, const char *val)
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
            free(item->valuestring);
            item->valuestring = strdup(val);
        }
        else
        {
            cJSON_AddStringToObject(s_root, key, val);
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t svc_param_save(void)
{
    FILE *fp;
    char *str;

    pthread_mutex_lock(&s_mutex);
    str = cJSON_PrintUnformatted(s_root);
    pthread_mutex_unlock(&s_mutex);

    if (str == NULL)
    {
        return SW_ERR_STORAGE;
    }

    fp = fopen(SVC_PARAM_FILE_PATH, "w");
    if (fp == NULL)
    {
        free(str);
        LOG_ERROR("svc_param_save: cannot open %s", SVC_PARAM_FILE_PATH);
        return SW_ERR_STORAGE;
    }

    fputs(str, fp);
    fclose(fp);
    free(str);
    LOG_INFO("svc_param: saved to %s", SVC_PARAM_FILE_PATH);
    return SW_OK;
}
