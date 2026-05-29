# 实施计划：标准化 JSON 配置导入

**分支**：`011-json-parser-migration` | **日期**：2026-05-11 | **规格**：[spec.md](./spec.md)  
**输入**：来自 `specs/011-json-parser-migration/spec.md` 的功能规格

**说明**：本计划围绕“把程序配置导入从项目自定义 JSON 语法解析实现迁移到标准 JSON 解析库，并删除原有自定义解析函数”展开。目标不是重做洗车程序领域模型，也不是扩展配置格式，而是在保留当前 `segments` 导入契约、仓储集成和失败语义的前提下，把 1119 行的自定义语法解析实现替换为成熟标准库，并让维护面收敛到字段映射与业务校验，同时彻底移除旧 `stages` 语义。

## 概要

本次特性保持 `json_program_parser_parse(...)` 这一公开入口不变，继续服务于文件仓储、测试辅助和配置契约验证；变更核心是把 `src/adapters/config/json_program_parser.c` 里自定义的 JSON 词法/语法解析逻辑整体移除，改为基于 vendored `cJSON` 完成标准 JSON 解码，再由本项目负责最小必要的对象键重复校验、字段提取、枚举解释和领域校验。之所以选择 vendored `cJSON`，是因为其官方定位就是“单个 C 源文件 + 单个头文件”的 ANSI C 解析库，能在不引入系统安装依赖的前提下最小化构建噪声；与此同时，cJSON 官方也明确说明它允许重复对象成员，因此本计划会增加一层很薄的重复键验证以保留现有“重复键必须拒绝”的契约。

实现后的主路径仍然是：读取程序配置文件 -> 解析 JSON 文档 -> 校验结构与重复键 -> 映射到 `wash_program_t` -> 运行现有 `program_validation_validate(...)`。本次迁移不会恢复旧 `stages` 架构；相反，所有遗留 `stages` 样例都应删除或迁移为当前 `segments` 语义，也不会扩展新 JSON 字段。

## 技术上下文

**语言/版本**：C17  
**主要依赖**：CMake/CTest、现有 `wash_program_t`/`program_validation_validate(...)`、vendored `cJSON`、文件仓储适配器  
**存储**：文件系统中的程序 JSON 配置；导入结果保存在进程内 `wash_program_t` 对象中；不新增数据库或额外持久化  
**测试方式**：CTest 驱动的 `unit` / `contract` / `integration` 测试，重点覆盖 `test_unit_program_config_validation`、`test_contract_program_config_adapter` 及依赖程序导入的运行时测试  
**目标平台**：Linux 单进程主控仿真环境；开发工作流运行在当前 CMake 工程下  
**项目类型**：单项目 C 静态库 + 主程序  
**性能目标**：程序配置导入仍然是加载时路径，不改变控制周期与运行时调度行为；迁移后导入延迟保持在现有测试可接受范围内，不引入额外后台线程或长期驻留状态  
**约束**：保持 `json_program_parser_parse(...)` 公开签名不变；保持 `segments` 架构与当前字段语义不变；继续拒绝重复键与非法配置；旧 `stages` 语义必须删除且不再兼容；不依赖系统预装 JSON 库；删除原有自定义 JSON 词法/语法解析函数；新增或修改的公开接口继续满足中文 Doxygen 要求  
**规模/范围**：核心修改集中在 `adapters/config/json_program_parser`、`src/CMakeLists.txt`、新增 vendored JSON 库目录，以及与程序导入契约相关的测试和文档

## 宪章检查

- 编码前先澄清：已明确“只替换 JSON 语法解析层、不改变领域契约”的前提；库选型、重复键处理和旧架构边界已在研究中收敛，无阻塞性未决问题。
- 简单优先：采用单文件 vendored 标准库接入，避免引入系统安装依赖、额外包管理流程或新的抽象层；重复键兼容仅补一层最小验证逻辑。
- 变更要精确：改动限定在程序配置导入模块、构建清单、vendored 库隔离目录和必要回归测试；不触碰主控生命周期、调度语义和无关领域模型。
- 以目标驱动执行：每一步都围绕“合法配置继续成功、无效配置继续失败、旧解析函数消失、仓储入口不变”定义验证项。
- 嵌入式 C 规范：所有新增 C 代码继续遵守 `snake_case`、返回值检查、指针判空、边界检查和公开接口中文 Doxygen；vendored 上游代码与本项目代码物理隔离，避免混写风格。
- 中文规范：计划、研究、数据模型、契约和 quickstart 全部使用中文，仅保留必要标识符、库名和路径。

**宪章豁免**：无

## 假设、问题与权衡

**假设**

- 当前正式受支持的程序配置契约以 `segments` 顶层结构为准；现有解析器对 `stages` 已显式返回 `ERROR_CODE_UNSUPPORTED`，本轮迁移继续保持这一拒绝语义。
- 现有 `tests/fixtures/wash_step_control/*.json` 中符合 `segments` 契约的样例，以及迁移后修正过的 `configs/programs/*.json` 有效样例，是迁移前后行为等价性的基线；旧 `stages` 样例不属于兼容基线。
- `program_validation_validate(...)` 继续承担领域语义校验职责；本次不把语义校验搬进标准 JSON 库层。
- 构建环境当前没有 repo 内现成 JSON 依赖，因此实现应尽量避免要求用户先安装系统级 JSON 开发包。

**未决问题**

- 无。研究阶段已经收敛到明确库接入和兼容策略。

**被拒绝的方案**

- 保留原自定义解析器，只在外层包一层标准库适配：无法达成“删除原自定义解析函数”的目标，维护面没有实质收缩。
- 依赖系统安装的 JSON 库（如 `apt install` / vcpkg / 全局开发包）：会让当前 CMake 工程在不同机器上出现不确定依赖前提，不符合最小可执行方案。
- 选择 `Jansson` 作为本次集成库：其官方文档提供 `JSON_REJECT_DUPLICATES` 等更强解码能力，但相较 `cJSON` 的单文件接入，vendoring 成本和构建接入面更大，不利于本次以最小改动完成迁移。
- 借迁移顺手恢复或兼容旧 `stages` 结构：这会扩大范围并改变当前正式契约，且与用户要求“旧 stages 应删除，只保留 segments 语义”直接冲突。

**最小可行方案**

- 在仓库中新增隔离的 vendored `cJSON` 源码目录，并把其纳入 `wash_core` 构建。
- 重写 `json_program_parser.c`，只保留公开入口和领域映射职责；原有自定义 JSON 词法/语法解析函数全部删除。
- 基于 `cJSON` 完成文档解析、对象/数组/字符串/数字/布尔值读取，并增加最小重复键校验以保留现有失败语义。
- 把仓库内仍残留旧 `stages` 语义的样例配置迁移为 `segments` 样例或删除，不再把它们视为受支持输入。
- 保持 `file_program_repository.c` 与测试辅助调用入口不变，仅复用迁移后的统一解析实现。
- 用现有 unit/contract 测试和必要补充测试验证合法配置、重复键、非法字段和仓储集成路径。

**范围边界**

- 会改：`include/adapters/config/json_program_parser.h`、`src/adapters/config/json_program_parser.c`、`src/CMakeLists.txt`、与程序导入相关的测试文件、`AGENTS.md` 计划指针，以及新增的 vendored JSON 库目录
- 不会改：`wash_program_t` 领域结构、主控 runtime 生命周期、调度器语义、事件日志、CLI 命令处理、洗车业务规则、与 JSON 导入无关的适配器

## 嵌入式 C 安全计划

**是否适用**：是  
**命名与分层**：本项目新增或改写代码继续使用 `snake_case`、宏全大写、类型 `_t` 结尾；公开声明继续放在 `include/adapters/config/`，实现留在 `src/adapters/config/`；vendored `cJSON` 与本项目代码隔离放置，不混入业务目录  
**格式与常量**：本项目代码保持 4 空格缩进、禁 Tab、控制语句带花括号；解析状态、对象类型和错误分支使用具名常量或明确辅助函数，不引入新的魔法数字  
**错误策略**：参数为空、文件读取失败、JSON 语法非法、重复键、字段缺失、类型错误、旧架构输入和领域校验失败都必须映射为明确 `operation_result_t`；不得静默忽略字段，也不得把导入失败延后到运行期  
**等待策略**：不适用；本特性不新增等待、轮询或重试模型  
**中断策略**：不适用；本特性不涉及 ISR、中断或硬件时序逻辑  
**并发与内存安全**：`cJSON` 解析树只在单次导入调用内持有并在返回前释放；所有文件缓冲区、字符串拷贝、对象访问和数组迭代都继续做边界检查与判空；失败路径不得遗留半有效 `wash_program_t`  
**状态机**：不适用；本特性不新增运行态状态机，只涉及导入流程的同步失败/成功闭环  
**注释风格**：保留 `json_program_parser.h` 的中文 Doxygen；本项目新增私有辅助函数仅在复杂兼容逻辑处写简洁中文注释；vendored 上游文件不做无关本地化改写

## 项目结构

### 本功能文档
```text
specs/011-json-parser-migration/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- program-config-import-contract.md
|   `-- repository-program-load-contract.md
`-- tasks.md
```

### 源码结构（仓库根目录）
```text
src/
|-- adapters/
|   |-- config/
|   |-- inbound/
|   |-- logging/
|   `-- outbound/
|-- application/
|   |-- coordinators/
|   `-- use_cases/
|-- domain/
|   |-- model/
|   `-- services/
|-- main/
`-- platform/
    |-- drivers/
    `-- linux/

include/
|-- adapters/
|-- application/
|-- domain/
|-- platform/
`-- shared/

tests/
|-- contract/
|-- integration/
|-- simulation/
`-- unit/

third_party/
`-- cjson/
```

**结构决策**：保持现有单项目 C 工程结构不变，只新增一个最小 `third_party/cjson/` 目录隔离 vendored 上游依赖；业务解析入口仍留在 `adapters/config`，这样既不污染领域层，也不会把上游源码混入本项目业务实现目录。

## 设计后复核结果

- Phase 0 已确认 `cJSON` 最符合当前“最小接入、无系统依赖”的构建约束，同时其官方文档也明确提示重复对象成员不会被自动拒绝，因此兼容策略必须在本项目内显式补齐。
- Phase 1 已把“配置文件 -> 标准库 JSON 树 -> 重复键/结构校验 -> `wash_program_t` -> 领域校验”的数据流、失败边界和公共契约写清楚，足以支撑后续任务拆解。
- 计划保持变更聚焦在 JSON 导入层，不扩展旧架构兼容、不重构仓储接口，也不把领域语义判断下沉到第三方库层。

## 复杂度跟踪

- 无新增宪章豁免。
