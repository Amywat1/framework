# 数据模型：标准化 JSON 配置导入

## 1. Program Config Source

**用途**  
表示程序配置导入的原始输入，即从文件系统读取的 JSON 文本来源。

**核心字段**

- `const char *config_path`
  - 配置文件路径
  - 由调用方提供给 `json_program_parser_parse(...)`
- `char buffer[]`
  - 文件读取后的原始 JSON 文本缓冲区
  - 只在单次导入调用期间有效

**约束**

- `config_path` 不能为空，且文件必须可读取。
- 原始文本必须能被标准 JSON 库完整解析为单个根对象。
- 读取失败时不得进入后续映射阶段。

## 2. Parsed JSON Document

**用途**  
表示由标准 JSON 解析库生成的中间文档树，供后续结构校验和字段提取使用。

**核心字段**

- `root`
  - JSON 根对象
- `object members`
  - 各层对象成员集合
- `array items`
  - `segments`、`conditional_controls` 等数组元素集合
- `primitive values`
  - 字符串、数字、布尔值等基础类型

**约束**

- 根节点必须是对象。
- 顶层和嵌套对象都必须通过重复键校验。
- 中间文档树只在导入调用内持有，返回前必须释放。

## 3. Duplicate Key Validation Result

**用途**  
表示针对 JSON 对象成员唯一性的校验结果，用于保留当前“重复键必须失败”的配置契约。

**核心字段**

- `object_path`
  - 出现问题的对象路径，例如顶层、某个 `segments[i]` 或触发器对象
- `duplicate_key`
  - 重复的成员名
- `is_valid`
  - 是否通过唯一性校验

**约束**

- 任一对象只要出现同名键，即判定整个配置导入失败。
- 重复键失败属于导入阶段失败，不得生成半有效 `wash_program_t`。

## 4. Imported Wash Program

**用途**  
表示从 JSON 配置导入后得到的强类型领域对象，即 `wash_program_t`。

**核心字段**

- `program_id`
- `program_name`
- `revision`
- `enabled`
- `default_segment_timeout_ms`
- `default_exit_timeout_ms`
- `segments[]`

**约束**

- 字段映射语义必须与迁移前保持一致。
- 必填字段缺失、类型不匹配或枚举文本无法解释时，导入必须失败。
- 导入完成后仍需通过 `program_validation_validate(...)` 的领域校验。

## 5. Parse Outcome

**用途**  
表示程序导入入口的最终结果语义，对应 `operation_result_t`。

**状态**

- `success`
  - 文件读取成功
  - JSON 语法解析成功
  - 重复键/结构校验通过
  - `wash_program_t` 映射成功
  - 领域校验通过
- `failure`
  - 任一阶段失败，且不保留可继续使用的程序对象

**典型失败原因**

- 参数为空
- 文件不存在或不可读
- JSON 语法错误
- 对象存在重复键
- 根结构不是对象
- 输入仍使用不受支持的 `stages` 架构
- 字段缺失、类型错误或枚举非法
- 领域顺序与控制规则校验失败

## 6. Repository Load View

**用途**  
表示文件仓储对程序导入结果的消费方式。

**核心字段**

- `program_id`
  - 调用方请求的程序标识
- `config_root`
  - 仓储配置根目录
- `wash_program`
  - 解析成功后的输出对象
- `runtime_program cache`
  - 仓储中已注入的运行时程序缓存

**约束**

- 仓储仍通过 `<config_root>/programs/<program_id>.json` 定位文件。
- 当导入失败时，仓储加载必须返回失败，不得伪造默认程序。
- 运行时缓存程序路径不应受本次标准库迁移影响。

## 7. 导入流程状态转换

```text
未开始
    |
    | 读取文件成功
    v
原始文本已加载
    |
    | 标准库解析成功
    v
JSON 文档已生成
    |
    | 重复键/结构校验通过
    v
可映射状态
    |
    | 字段映射 + 领域校验通过
    v
导入成功
```

**异常路径**

- `未开始 -> 文件读取失败 -> 导入失败`
- `原始文本已加载 -> JSON 语法错误 -> 导入失败`
- `JSON 文档已生成 -> 重复键或结构非法 -> 导入失败`
- `可映射状态 -> 字段/语义校验失败 -> 导入失败`

**安全要求**

- 任一失败路径都必须释放中间 JSON 文档资源。
- 任一失败路径都不得让外层仓储或测试入口误认为存在有效程序对象。
