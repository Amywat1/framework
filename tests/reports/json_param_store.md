# json_param_store 单元测试报告

| 属性 | 值 |
|------|-----|
| 测试目标 | `test_json_param_store` |
| 源文件 | `tests/adapters/test_json_param_store.c` |
| 被测代码 | `adapters/outbound/storage/json/json_param_store.c` |
| 关联端口 | `ports/outbound/storage/param_store.h`、`ports/port_registry.c` |
| 测试文件 | `${CMAKE_BINARY_DIR}/tests/param_store_test.json`（编译宏注入） |
| 最后执行 | 2026-07-10 |
| 结果 | **12/12 通过** |

## 测试环境

- 每个用例 `setUp()` 删除临时 JSON 并 `json_param_store_register()`
- `PARAM_STORE_JSON_FILE_PATH` 由 CMake 编译宏注入，路径在 build 目录内
- 用例间通过 `load()` / 删文件隔离状态

## 用例明细

### A. load（4 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_load_file_not_found` | 文件缺失 | 返回 `SW_ERR_STORAGE` | 通过 |
| `test_load_valid_json_string_key` | 加载字符串键 | load OK，get 返回 `"DEV-001"` | 通过 |
| `test_load_valid_json_number_key` | 加载数值键 | get 返回 `"120"` | 通过 |
| `test_load_invalid_json` | 非法 JSON | 返回 `SW_ERR_STORAGE` | 通过 |

### B. get（3 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_get_missing_key` | 键不存在 | 返回 `SW_ERR_PARAM` | 通过 |
| `test_get_null_args` | 空指针/零长度缓冲 | 返回 `SW_ERR_PARAM` | 通过 |
| `test_get_truncates_long_string` | 缓冲区截断 | 8 字节缓冲得到 7 字符 + `\0` | 通过 |

### C. set（4 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_set_and_get_new_key` | load 失败后内存写入 | set/get 新键 `"auto"` | 通过 |
| `test_set_update_string_key` | 更新字符串键 | `"manual"` → `"auto"` | 通过 |
| `test_set_update_number_key` | 更新数值键 | `10` → `"20"` | 通过 |
| `test_set_null_args` | 参数校验 | 返回 `SW_ERR_PARAM` | 通过 |

### D. save（2 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_save_and_reload` | 持久化往返 | save 后 reload，值一致 | 通过 |
| `test_save_empty_json_object` | 空存储持久化 | load 失败后 save，reload 成功 | 通过 |

## 未覆盖项

| 场景 | 说明 |
|------|------|
| 并发读写 | 多线程 get/set 压力未测 |
| 空文件 load | 文件存在但长度为 0 |
| 超大 JSON | 内存与解析边界 |
| `s_root == NULL` 时 save | 仅进程冷启动、未 load 前；共享测试进程难以隔离 |

## 待补充

| 用例 | 目的 | 状态 |
|------|------|------|
| — | — | — |
