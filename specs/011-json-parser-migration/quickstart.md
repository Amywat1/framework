# Quickstart：将程序配置导入迁移到标准 JSON 库

## 1. 接入标准库
1. 在仓库内引入隔离的 `third_party/cjson/` 目录，保存 vendored `cJSON.c`、`cJSON.h` 与许可证。
2. 更新 `src/CMakeLists.txt`，把 `third_party/cjson/cJSON.c` 纳入 `wash_core`，并补上 `third_party/cjson` 头文件路径。
3. Linux 构建继续通过项目自己的 CMake/CTest 流程完成，不依赖系统额外安装 JSON 开发包。

## 2. 替换解析器内部实现
1. 保留公开入口 `json_program_parser_parse(const char *config_path, wash_program_t *wash_program)`。
2. 删除 `src/adapters/config/json_program_parser.c` 中原有自定义 JSON 词法/语法解析骨架。
3. 改为使用 `cJSON` 完成：
   - 根对象解析
   - 成员读取与类型校验
   - `segments` 数组遍历
   - 字符串、整数、布尔值提取
4. 在字段映射前递归检测对象重复键，继续拒绝重复键输入。
5. 继续复用现有枚举解释和 `program_validation_validate(...)` 领域校验。

## 3. 保持统一导入路径
1. `file_program_repository.c` 继续只通过 `json_program_parser_parse(...)` 加载 `<config_root>/programs/<program_id>.json`。
2. `tests/test_support.h` 继续通过同一公开入口把夹具导入为 `wash_program_t`。
3. 新增的集成回归需要同时覆盖：
   - 直接解析夹具
   - 仓储加载配置文件
   - 测试辅助将夹具注入运行时缓存

## 4. 聚焦验证命令
建议至少运行以下聚焦回归：

```powershell
wsl.exe bash -lc "cd /mnt/c/Users/HUAWEI/Desktop/Code-M8 && cmake --build build -j2 && cd build && ctest --output-on-failure -R 'test_unit_program_config_validation|test_contract_program_config_adapter|test_contract_program_config_contract|test_integration_program_repository_load_path|test_integration_system_context_error_paths'"
```

## 5. 通过标准
- 合法 `segments` 配置夹具继续成功导入。
- `standard_wash.json`、`heavy_wash.json` 与 `wash_step_control_v1.json` 不再保留旧 `stages` 结构。
- JSON 语法错误、重复键、字段缺失、旧 `stages` 顶层结构和非法语义输入继续明确失败。
- 失败导入不会留下半有效 `wash_program_t`，也不会污染运行时程序缓存。
- 代码库中不再保留项目自定义 JSON 词法/语法解析函数。
