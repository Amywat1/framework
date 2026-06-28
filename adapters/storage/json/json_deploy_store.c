/**
 * @file    json_deploy_store.c
 * @brief   JSON 文件部署配置存储适配器（只读，实现 deploy_store_ops_t）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    读取 config/deployment/device.json，出厂写入，运行期只读。
 *          若文件不存在，所有 get() 调用返回 SW_ERR_PARAM（键未找到），
 *          调用方应提供硬编码默认值。
 */

#ifndef DEPLOY_STORE_JSON_FILE_PATH
#define DEPLOY_STORE_JSON_FILE_PATH  "/home/neardi/m8/device.json"
#endif

#include "ports/storage/deploy_store.h"
#include "common/log.h"
#include "third_party/cJSON/cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static cJSON          *s_cfg   = NULL;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

static sw_err_t deploy_load(void)
{
    FILE    *fp;
    long     len;
    char    *buf = NULL;
    sw_err_t ret = SW_ERR_STORAGE;

    pthread_mutex_lock(&s_mutex);

    fp = fopen(DEPLOY_STORE_JSON_FILE_PATH, "r");
    if (fp == NULL)
    {
        LOG_WARN("json_deploy_store: file not found (%s)", DEPLOY_STORE_JSON_FILE_PATH);
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
                    if (s_cfg != NULL) { cJSON_Delete(s_cfg); }
                    s_cfg = parsed;
                    ret   = SW_OK;
                }
            }
            free(buf);
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&s_mutex);

    if (ret == SW_OK)
    {
        LOG_INFO("json_deploy_store: loaded from %s", DEPLOY_STORE_JSON_FILE_PATH);
    }
    return ret;
}

static sw_err_t deploy_get(const char *key, char *buf, size_t buf_size)
{
    sw_err_t ret = SW_ERR_PARAM;

    if ((key == NULL) || (buf == NULL) || (buf_size == 0U))
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_cfg != NULL)
    {
        cJSON *item = cJSON_GetObjectItem(s_cfg, key);
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
                snprintf(buf, buf_size, "%g", item->valuedouble);
                ret = SW_OK;
            }
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

static const deploy_store_ops_t s_ops = {
    .load = deploy_load,
    .get  = deploy_get,
};

void json_deploy_store_register(void)
{
    deploy_store_register(&s_ops);
    LOG_INFO("json_deploy_store: registered");
}
