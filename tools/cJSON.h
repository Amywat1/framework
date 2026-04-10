/**
 * @file    tools/cJSON.h
 * @brief   cJSON 转发头（根据构建目标选择来源）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    嵌入式目标（BUILD_SIM 未定义）：
 *            cJSON 由 snack SDK 提供，位于 /usr/local/include/snack/cJSON.h，
 *            通过 target_include_directories 中的 snack 路径可见。
 *          PC 仿真（BUILD_SIM=1）：
 *            使用系统 libcjson（sudo apt install libcjson-dev）。
 *            若系统未安装，会触发 #error 提示。
 */

#ifndef TOOLS_CJSON_WRAPPER_H
#define TOOLS_CJSON_WRAPPER_H

#ifdef BUILD_SIM

  /* PC 仿真：使用系统安装的 libcjson */
  #if defined(__has_include)
    #if __has_include(<cjson/cJSON.h>)
      #include <cjson/cJSON.h>
    #elif __has_include(<cJSON.h>)
      #include <cJSON.h>
    #else
      #error "cJSON not found for sim build. Run: sudo apt install libcjson-dev"
    #endif
  #else
    /* 不支持 __has_include 的编译器，假设 Debian/Ubuntu 标准路径 */
    #include <cjson/cJSON.h>
  #endif

#else

  /* 嵌入式目标：cJSON 由 snack SDK include 路径提供 */
  #include <cJSON.h>

#endif /* BUILD_SIM */

#endif /* TOOLS_CJSON_WRAPPER_H */
