/**
 * @file    engine_program_manifest.c
 * @brief   洗车方案 manifest 完整性校验实现
 * @author  huwangwei
 * @date    2026-07-08
 */

#include "domain/wash/model/engine_program_manifest.h"

#include "third_party/cJSON/cJSON.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_ERR_MAX 160U
#define SHA256_HEX_LEN   64U

/* -------------------------------------------------------------------------
 * SHA256（RFC 6234 精简实现，仅 manifest 校验使用）
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} sha256_ctx_t;

static const uint32_t s_k[64]
    = {0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
       0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
       0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
       0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
       0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
       0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
       0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
       0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

static uint32_t rotr32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32U - n));
}

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bitlen   = 0U;
    ctx->datalen  = 0U;
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t *data)
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    unsigned i;

    for (i = 0U; i < 16U; ++i) {
        w[i] = ((uint32_t)data[i * 4U] << 24) | ((uint32_t)data[i * 4U + 1U] << 16) | ((uint32_t)data[i * 4U + 2U] << 8)
               | ((uint32_t)data[i * 4U + 3U]);
    }
    for (i = 16U; i < 64U; ++i) {
        uint32_t s0 = rotr32(w[i - 15U], 7) ^ rotr32(w[i - 15U], 18) ^ (w[i - 15U] >> 3);
        uint32_t s1 = rotr32(w[i - 2U], 17) ^ rotr32(w[i - 2U], 19) ^ (w[i - 2U] >> 10);
        w[i]        = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0U; i < 64U; ++i) {
        uint32_t s1  = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch  = (e & f) ^ ((~e) & g);
        uint32_t t1  = h + s1 + ch + s_k[i] + w[i];
        uint32_t s0  = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2  = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i = 0U;

    while (i < len) {
        size_t offset = ctx->datalen;
        size_t room   = 64U - offset;
        size_t copy   = (len - i < room) ? (len - i) : room;

        (void)memcpy(ctx->data + offset, data + i, copy);
        ctx->datalen += (uint32_t)copy;
        i += copy;

        if (ctx->datalen == 64U) {
            sha256_transform(ctx, ctx->data);
            ctx->datalen = 0U;
        }
    }
    ctx->bitlen += (uint64_t)len * 8U;
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32])
{
    uint32_t i    = ctx->datalen;
    uint64_t bits = ctx->bitlen;

    ctx->data[i++] = 0x80U;
    if (i > 56U) {
        while (i < 64U) {
            ctx->data[i++] = 0U;
        }
        sha256_transform(ctx, ctx->data);
        i = 0U;
    }
    while (i < 56U) {
        ctx->data[i++] = 0U;
    }

    ctx->data[56] = (uint8_t)(bits >> 56);
    ctx->data[57] = (uint8_t)(bits >> 48);
    ctx->data[58] = (uint8_t)(bits >> 40);
    ctx->data[59] = (uint8_t)(bits >> 32);
    ctx->data[60] = (uint8_t)(bits >> 24);
    ctx->data[61] = (uint8_t)(bits >> 16);
    ctx->data[62] = (uint8_t)(bits >> 8);
    ctx->data[63] = (uint8_t)(bits);

    sha256_transform(ctx, ctx->data);

    for (i = 0U; i < 8U; ++i) {
        out[i * 4U]      = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4U + 1U] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4U + 2U] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4U + 3U] = (uint8_t)(ctx->state[i]);
    }
}

static void sha256_hex(const uint8_t digest[32], char out[SHA256_HEX_LEN + 1U])
{
    static const char hex[] = "0123456789abcdef";
    unsigned          i;

    for (i = 0U; i < 32U; ++i) {
        out[i * 2U]      = hex[digest[i] >> 4];
        out[i * 2U + 1U] = hex[digest[i] & 0x0fU];
    }
    out[SHA256_HEX_LEN] = '\0';
}

/* -------------------------------------------------------------------------
 * 辅助
 * ------------------------------------------------------------------------- */
static void mfail(char *err, unsigned errsz, const char *fmt, const char *arg)
{
    if ((err != NULL) && (errsz > 0U)) {
        (void)snprintf(err, errsz, fmt, arg);
    }
}

static bool read_file_bytes(const char *path, uint8_t **out_buf, long *out_sz, char *err, unsigned errsz)
{
    FILE *fp = fopen(path, "rb");

    *out_buf = NULL;
    *out_sz  = 0L;

    if (fp == NULL) {
        mfail(err, errsz, "无法打开文件: %s", path);
        return false;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        mfail(err, errsz, "读取文件失败: %s", path);
        (void)fclose(fp);
        return false;
    }

    long sz = ftell(fp);
    if (sz < 0) {
        mfail(err, errsz, "读取文件失败: %s", path);
        (void)fclose(fp);
        return false;
    }

    (void)fseek(fp, 0L, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz + 1U);
    if (buf == NULL) {
        mfail(err, errsz, "%s", "内存不足");
        (void)fclose(fp);
        return false;
    }

    size_t rd = fread(buf, 1U, (size_t)sz, fp);
    (void)fclose(fp);

    if ((long)rd != sz) {
        mfail(err, errsz, "读取文件失败: %s", path);
        free(buf);
        return false;
    }

    *out_buf = buf;
    *out_sz  = sz;
    return true;
}

static sw_err_t hash_file_sha256_hex(const char *path,
                                     char        out_hex[SHA256_HEX_LEN + 1U],
                                     long       *out_size,
                                     char       *err,
                                     unsigned    errsz)
{
    uint8_t     *buf = NULL;
    long         sz  = 0L;
    sha256_ctx_t ctx;
    uint8_t      digest[32];

    if (!read_file_bytes(path, &buf, &sz, err, errsz)) {
        return SW_ERR_PARAM;
    }

    if (out_size != NULL) {
        *out_size = sz;
    }

    sha256_init(&ctx);
    sha256_update(&ctx, buf, (size_t)sz);
    sha256_final(&ctx, digest);
    free(buf);

    sha256_hex(digest, out_hex);
    return SW_OK;
}

bool engine_program_manifest_path_from_json(const char *json_path, char *out, unsigned outsz)
{
    size_t len;

    if ((json_path == NULL) || (out == NULL) || (outsz == 0U)) {
        return false;
    }

    len = strlen(json_path);
    if ((len < 5U) || (strcmp(json_path + len - 5U, ".json") != 0)) {
        return false;
    }

    if (outsz <= (len + 10U)) {
        return false;
    }

    (void)snprintf(out, outsz, "%.*s.manifest.json", (int)(len - 5), json_path);
    return true;
}

sw_err_t engine_program_manifest_verify(const char *json_path, const char *manifest_path, char *err, unsigned errsz)
{
    char         local_err[MANIFEST_ERR_MAX];
    char        *werr = (err != NULL && errsz > 0U) ? err : local_err;
    unsigned     wsz  = (err != NULL && errsz > 0U) ? errsz : (unsigned)sizeof(local_err);
    uint8_t     *mbuf = NULL;
    long         msz  = 0L;
    char         computed[SHA256_HEX_LEN + 1U];
    long         json_size = 0L;
    cJSON       *root      = NULL;
    const cJSON *sha_item;
    const cJSON *size_item;
    const char  *expect_sha;
    int          expect_size;

    werr[0] = '\0';

    if ((json_path == NULL) || (manifest_path == NULL)) {
        mfail(werr, wsz, "%s", "路径为空");
        return SW_ERR_PARAM;
    }

    if (hash_file_sha256_hex(json_path, computed, &json_size, werr, wsz) != SW_OK) {
        return SW_ERR_PARAM;
    }

    if (!read_file_bytes(manifest_path, &mbuf, &msz, werr, wsz)) {
        return SW_ERR_PARAM;
    }

    root = cJSON_ParseWithLength((const char *)mbuf, (size_t)msz);
    free(mbuf);
    if (root == NULL) {
        mfail(werr, wsz, "%s", "manifest JSON 解析失败");
        return SW_ERR_PARAM;
    }

    sha_item  = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    size_item = cJSON_GetObjectItemCaseSensitive(root, "size");

    if ((sha_item == NULL) || !cJSON_IsString(sha_item)) {
        mfail(werr, wsz, "%s", "manifest 缺少 sha256");
        cJSON_Delete(root);
        return SW_ERR_PARAM;
    }

    if ((size_item == NULL) || !cJSON_IsNumber(size_item)) {
        mfail(werr, wsz, "%s", "manifest 缺少 size");
        cJSON_Delete(root);
        return SW_ERR_PARAM;
    }

    expect_sha  = sha_item->valuestring;
    expect_size = size_item->valueint;

    if ((expect_sha == NULL) || (strlen(expect_sha) != SHA256_HEX_LEN)) {
        mfail(werr, wsz, "%s", "manifest sha256 格式错误");
        cJSON_Delete(root);
        return SW_ERR_CRC;
    }

    if (expect_size != (int)json_size) {
        mfail(werr, wsz, "%s", "manifest size 不匹配");
        cJSON_Delete(root);
        return SW_ERR_CRC;
    }

    if (strcmp(expect_sha, computed) != 0) {
        mfail(werr, wsz, "%s", "manifest sha256 不匹配");
        cJSON_Delete(root);
        return SW_ERR_CRC;
    }

    cJSON_Delete(root);
    return SW_OK;
}
