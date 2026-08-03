/**
 * @file    json_deploy_store.c
 * @brief   JSON 文件部署配置存储适配器（只读，实现 deploy_store_ops_t）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    读取路径由 json_deploy_store_configure() 注入，编译宏只作为默认
 *          fallback；出厂写入，运行期只读。
 *          若文件不存在，所有 get() 调用返回 SW_ERR_PARAM（键未找到），
 *          调用方应提供硬编码默认值。
 */

#include "adapters/outbound/storage/json/json_deploy_store.h"

#ifndef DEPLOY_STORE_JSON_FILE_PATH
#define DEPLOY_STORE_JSON_FILE_PATH ""
#endif

#include "common/log.h"
#include "ports/outbound/storage/deploy_store.h"
#include "third_party/cJSON/cJSON.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_DEPLOY_STORE_PATH_MAX 256U

static cJSON          *s_cfg                                   = NULL;
static pthread_mutex_t s_mutex                                 = PTHREAD_MUTEX_INITIALIZER;
static char            s_file_path[JSON_DEPLOY_STORE_PATH_MAX] = DEPLOY_STORE_JSON_FILE_PATH;

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

static sw_err_t deploy_load(void)
{
    FILE    *fp;
    long     len;
    char    *buf = NULL;
    char     path[JSON_DEPLOY_STORE_PATH_MAX];
    sw_err_t ret = SW_ERR_STORAGE;

    if (get_file_path(path, sizeof(path)) != SW_OK) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);

    fp = fopen(path, "r");
    if (fp == NULL) {
        LOG_WARN("json_deploy_store: file not found (%s)", path);
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
                    if (s_cfg != NULL) {
                        cJSON_Delete(s_cfg);
                    }
                    s_cfg = parsed;
                    ret   = SW_OK;
                }
            }
            free(buf);
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&s_mutex);

    if (ret == SW_OK) {
        LOG_INFO("json_deploy_store: loaded from %s", path);
    }
    return ret;
}

static sw_err_t deploy_get(const char *key, char *buf, size_t buf_size)
{
    sw_err_t ret = SW_ERR_PARAM;

    if ((key == NULL) || (buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_cfg != NULL) {
        cJSON *item = cJSON_GetObjectItem(s_cfg, key);
        if (item != NULL) {
            if (cJSON_IsString(item) && (item->valuestring != NULL)) {
                strncpy(buf, item->valuestring, buf_size - 1U);
                buf[buf_size - 1U] = '\0';
                ret                = SW_OK;
            } else if (cJSON_IsNumber(item)) {
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
    /* s_ops 静态定义且必填字段齐全，注册不应失败；失败即为编程错误 */
    if (deploy_store_register(&s_ops) != SW_OK) {
        LOG_ERROR("json_deploy_store: register rejected (ops incomplete)");
        return;
    }
    LOG_INFO("json_deploy_store: registered");
}

sw_err_t json_deploy_store_configure(const char *path)
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
