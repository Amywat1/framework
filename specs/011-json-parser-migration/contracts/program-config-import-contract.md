# 契约：程序配置 JSON 导入

## 目标
定义 `json_program_parser_parse(const char *config_path, wash_program_t *wash_program)` 在标准库迁移后的稳定行为边界。

## 输入契约
- `config_path` 不能为空，且必须指向可读取的 JSON 配置文件。
- `wash_program` 不能为空，且仅在解析成功时才可被外层视为有效输出。
- 根节点必须是 JSON 对象。
- 当前正式支持的顶层程序结构只有 `segments`。
- 输入若使用旧 `stages` 顶层结构，必须返回不支持结果。
- 对象成员名按 JSON 标准大小写敏感处理。
- 任意对象出现重复键时，整个导入必须失败。

## 成功契约
- 返回 `operation_result_t.ok == true`。
- 输出的 `wash_program_t` 与迁移前对同一合法 `segments` 配置的核心语义保持等价。
- 合法配置样例继续满足：
  - `program_id`、`program_name`、`revision`、`enabled` 正确映射
  - `segment_count` 正确
  - `motion_plan`、`continuous_controls`、`conditional_controls`
    、`completion_condition`、`exit_actions`、`exception_policy` 正确映射
  - 缺省的 `segment_timeout_ms` 与 `exit_timeout_ms` 会回落到程序默认值
  - 导入后通过 `program_validation_validate(...)`

## 失败契约
- 参数为空时，返回 `ERROR_CODE_INVALID_ARGUMENT`。
- 文件读取失败时，返回 `ERROR_CODE_IO_FAILED`。
- JSON 语法错误、重复键、字段缺失、字段类型错误或整数值非法时，返回 `ERROR_CODE_PARSE_FAILED`。
- 使用旧 `stages` 顶层结构时，返回 `ERROR_CODE_UNSUPPORTED`。
- 使用不受支持的条件控制对象时，返回 `ERROR_CODE_UNSUPPORTED`。
- 领域语义非法时，继续返回当前契约定义的失败结果。
- 任一失败场景下，不得保留半有效 `wash_program_t` 输出。

## 兼容要求
- 不新增第二套公开导入入口。
- 不改变 `json_program_parser_parse(...)` 的调用方式。
- 不要求调用方直接感知底层使用的具体标准库类型。

## 验证关注点
- 合法配置夹具继续通过。
- 边界字符配置继续通过。
- 重复键继续失败。
- JSON 语法错误继续失败。
- 旧 `stages` 顶层结构继续失败。
- 缺失字段、非法位置语义和非法顺序继续失败。
