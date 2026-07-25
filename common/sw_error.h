/**
 * @file    sw_error.h
 * @brief   项目统一错误码定义
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef SW_ERROR_H
#define SW_ERROR_H

/* -------------------------------------------------------------------------
 * 错误码类型
 * ------------------------------------------------------------------------- */
typedef enum {
    SW_OK              = 0,   /* 成功 */
    SW_ERR_PARAM       = -1,  /* 参数非法 */
    SW_ERR_TIMEOUT     = -2,  /* 超时 */
    SW_ERR_HW          = -3,  /* 硬件错误 */
    SW_ERR_BUSY        = -4,  /* 资源忙 */
    SW_ERR_NOMEM       = -5,  /* 内存不足 */
    SW_ERR_OVERFLOW    = -6,  /* 缓冲区溢出 */
    SW_ERR_STATE       = -7,  /* 状态错误（当前状态不允许此操作）*/
    SW_ERR_CRC         = -8,  /* 校验失败 */
    SW_ERR_STORAGE     = -9,  /* 存储读写失败 */
    SW_ERR_COMM        = -10, /* 通信错误 */
    SW_ERR_UPGRADE     = -11, /* 升级错误 */
    SW_ERR_NOT_INIT    = -12, /* 模块未初始化 */
    SW_ERR_NOT_SUPPORT = -13, /* 功能不支持 */
    SW_ERR_NOT_FOUND   = -14, /* 目标记录或资源不存在 */
} sw_err_t;

#endif /* SW_ERROR_H */
