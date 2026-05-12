# 研究记录：标准化 JSON 配置导入

## 决策 1：采用 vendored `cJSON` 作为标准 JSON 语法解析层

**Decision**  
将程序配置导入的 JSON 语法解析层切换为 vendored `cJSON`，并把上游源码隔离放在仓库内单独目录中参与当前 CMake 构建。

**Rationale**  
`cJSON` 官方 README 明确说明它是“单个 C 源文件 + 单个头文件”的 ANSI C 库，并支持直接把源文件复制进项目使用。这与当前仓库“单项目 C 静态库 + 手写源文件列表”的构建方式最匹配，也能避免依赖系统已安装开发包或额外包管理流程。对本次需求而言，最重要的是用最小构建噪声替换掉现有自定义语法解析逻辑，而不是引入一个更重的依赖接入面。

**Alternatives considered**  
- `Jansson`：官方文档显示它无外部依赖、支持 Windows/Unix，并且 API 稳定；但 vendoring 体积和构建接入面都明显大于 `cJSON`。
- 依赖系统预装 JSON 库：实现表面更省代码，但会给当前仓库引入机器环境前提，不利于稳定构建。
- 继续维护当前自定义解析器：无法满足“改成标准库并删除原解析函数”的目标。

**参考来源**  
- cJSON 官方 README：<https://github.com/DaveGamble/cJSON>
- Jansson 官方文档：<https://jansson.readthedocs.io/>

## 决策 2：保留“重复键必须拒绝”的现有契约，由本项目补充最小重复键验证

**Decision**  
在标准库完成 JSON 解码后，由本项目增加一层很薄的对象成员重复键验证，继续把重复键输入判定为失败。

**Rationale**  
当前 unit 测试已经把“重复键必须失败，且返回解析失败结果”固化为公开契约。cJSON 官方文档明确指出它支持带重复对象成员的 JSON，而对象读取函数只会返回第一个同名成员；如果直接替换而不补偿，迁移会静默改变现有行为。相比重新引入自定义词法/语法解析器，一个递归对象成员校验是最小、可审计且符合用户目标的兼容补丁。

**Alternatives considered**  
- 直接接受 cJSON 的默认重复键行为：会破坏现有测试与配置契约，不可接受。
- 为了内建重复键拒绝改用 `Jansson`：可行，但会放大依赖接入面，不符合本次简单优先。
- 保留现有自定义对象解析仅用于重复键检测：等于没有真正删除旧解析函数，违背需求。

**参考来源**  
- cJSON 官方 README 的 Duplicate Object Members 说明：<https://github.com/DaveGamble/cJSON>
- Jansson `JSON_REJECT_DUPLICATES` 文档：<https://jansson.readthedocs.io/en/latest/apiref.html>

## 决策 3：公开入口与领域映射契约保持不变，只替换语法层

**Decision**  
保留 `json_program_parser_parse(const char *config_path, wash_program_t *wash_program)` 公开签名、仓储调用路径和现有 `wash_program_t` 映射语义，只替换底层 JSON 语法解析实现。

**Rationale**  
当前 `file_program_repository.c`、`tests/test_support.h`、unit/contract 测试以及运行时加载路径都以 `json_program_parser_parse(...)` 为统一入口。如果迁移时顺带改入口、改输出对象或扩展新字段，会把一次基础设施替换放大成 API 重构。保持入口和领域映射不变，可以把风险集中在最小必要范围内，并让现有测试直接充当迁移回归基线。

**Alternatives considered**  
- 新增第二套解析入口：会造成双轨维护。
- 同时重构 `wash_program_t` 或恢复旧 `stages` 结构：超出范围，并扩大行为变化面。

## 决策 4：回归验证以现有有效/无效夹具和仓储集成为主

**Decision**  
迁移后的验证重点放在已有的程序配置有效夹具、非法夹具、仓储加载路径和测试辅助入口，而不是新增一套与当前契约无关的解析演示路径。

**Rationale**  
现有仓库已经提供了合法配置、边界字符、重复键、缺失字段、非法顺序和非法语义等夹具，并且 `file_program_repository` 与 `test_support.h` 都直接依赖统一解析入口。这些路径天然就是最有价值的迁移回归矩阵，能直接证明“行为未退化”。

**Alternatives considered**  
- 新增独立 demo 程序验证：信息价值低于直接复用现有测试矩阵。
- 只做单元测试、不覆盖仓储入口：无法证明调用路径没有分叉。
