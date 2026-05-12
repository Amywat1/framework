# 任务：标准化 JSON 配置导入

**输入**：来自 `/specs/011-json-parser-migration/` 的设计文档  
**前置条件**：`plan.md`、`spec.md`、`research.md`、`data-model.md`、`contracts/`、`quickstart.md`

**验证原则**：先补测试与夹具，再落实现；最终以 `quickstart.md` 中的聚焦 `ctest` 回归为准。  
**范围约束**：只迁移程序配置 JSON 导入层，不扩展新 schema，不恢复旧 `stages` 语义。

## Phase 1：准备阶段
- [X] T001 在 `third_party/cjson/cJSON.c`、`third_party/cjson/cJSON.h`、`third_party/cjson/LICENSE` 引入 vendored `cJSON`。验证：第三方代码隔离在 `third_party/cjson/` 下。
- [X] T002 更新 `src/CMakeLists.txt` 与 `tests/CMakeLists.txt`，把 `third_party/cjson/cJSON.c` 纳入 `wash_core`，并注册 program-config 相关新旧测试目标。验证：`cmake --build build` 能进入构建。

## Phase 2：基础阶段
- [X] T003 更新 `include/adapters/config/json_program_parser.h`，固定 `json_program_parser_parse(...)` 的公开契约、失败语义和中文 Doxygen，且不改函数签名。
- [X] T004 在 `src/adapters/config/json_program_parser.c` 建立共享 cJSON 解析骨架，包括文件读取、文档释放、递归重复键扫描、对象/数组成员读取与统一错误出口。
- [X] T005 新增 `tests/fixtures/wash_step_control/program_v1_invalid_json_syntax.json` 与 `tests/fixtures/wash_step_control/program_v1_invalid_stages_schema.json`。验证：分别覆盖 JSON 语法错误和旧 `stages` 顶层结构。

## Phase 3：US1 稳定导入合法配置
- [X] T006 [P] [US1] 更新 `tests/unit/test_program_config_validation.c`，固定合法夹具、边界字符夹具和默认字段回落行为在迁移后继续成功导入。
- [X] T007 [P] [US1] 更新 `tests/contract/test_program_config_adapter_contract.c` 与 `tests/contract/test_program_config_contract.c`，固定直接导入与仓储样例都符合当前 `segments` 契约。
- [X] T008 [US1] 更新 `configs/programs/standard_wash.json` 与 `tests/contract/test_program_config_contract.c`，把仓储成功样例迁移到当前合法 `segments` 结构。
- [X] T009 [US1] 在 `src/adapters/config/json_program_parser.c` 基于 cJSON 实现顶层程序字段提取、根对象校验和 `segments` 数组遍历。
- [X] T010 [US1] 在 `src/adapters/config/json_program_parser.c` 实现 `motion_plan`、`continuous_controls`、`conditional_controls`、`completion_condition`、`exit_actions` 和 `exception_policy` 的嵌套字段映射。

## Phase 4：US2 显式拒绝无效配置
- [X] T011 [P] [US2] 更新 `tests/unit/test_program_config_validation.c`，固定 JSON 语法错误、重复键、缺失字段、非法顺序、非法位置语义和旧 `stages` 结构的失败断言。
- [X] T012 [P] [US2] 更新 `tests/integration/test_system_context_error_paths.c` 与 `tests/test_support.h`，固定导入失败不会留下运行时缓存污染。
- [X] T013 [US2] 在 `src/adapters/config/json_program_parser.c` 实现递归重复键检测、旧 `stages` 结构拒绝、必填字段检查和类型不匹配失败映射。
- [X] T014 [US2] 在 `src/adapters/config/json_program_parser.c` 完成失败路径上的 cJSON 资源释放、输出对象保护和统一 `operation_result_t` 错误出口。

## Phase 5：US3 移除自定义解析器
- [X] T015 [P] [US3] 新建 `tests/integration/test_program_repository_load_path.c`，固定“直接导入 / 仓储加载 / 测试辅助加载”三条路径得到等价程序对象。
- [X] T016 [P] [US3] 更新 `tests/CMakeLists.txt`、`tests/contract/test_program_config_adapter_contract.c` 与 `tests/test_support.h`，固定迁移后不再存在第二套 JSON 解析入口。
- [X] T017 [US3] 在 `src/adapters/config/json_program_parser.c` 删除原有自定义 JSON 词法/语法解析函数和相关数据结构，只保留 cJSON 驱动的私有辅助函数与领域映射实现。
- [X] T018 [US3] 审计并更新 `src/CMakeLists.txt`、`src/adapters/outbound/file_program_repository.c`、`tests/test_support.h` 与 `include/adapters/config/json_program_parser.h`，确保标准 JSON 库是唯一语法依赖且所有调用方仍只走统一公开入口。

## Phase 6：收尾与跨故事事项
- [X] T019 [P] 更新 `specs/011-json-parser-migration/quickstart.md`、`specs/011-json-parser-migration/contracts/program-config-import-contract.md` 与 `specs/011-json-parser-migration/contracts/repository-program-load-contract.md`，回写实际构建/回归命令、失败语义和迁移后验证重点。
- [X] T020 在 `build/` 下按 `quickstart.md` 运行聚焦回归，至少覆盖 `test_unit_program_config_validation`、`test_contract_program_config_adapter`、`test_contract_program_config_contract`、`test_integration_program_repository_load_path`、`test_integration_system_context_error_paths`。
- [X] T021 审计 `include/adapters/config/json_program_parser.h`、`src/adapters/config/json_program_parser.c`、`src/CMakeLists.txt` 与 `third_party/cjson/`，清理本次迁移引入的残留并确认第三方目录隔离。

## 依赖顺序
- Phase 1 完成后进入 Phase 2。
- Phase 2 完成后交付 US1。
- US1 稳定后补齐 US2 的失败语义。
- US1 与 US2 完成后再彻底移除旧解析器并完成 US3。
- 最后回写文档、任务状态并跑聚焦回归。

## 完成结果
- 总任务数：21
- 已完成：21
- 当前状态：实现完成，聚焦回归通过
