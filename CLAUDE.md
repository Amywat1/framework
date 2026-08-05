# wash-device-framework

面向洗车设备的通用嵌入式 C99 框架。稳定的设备业务能力沉淀在框架中，项目差异留在
wiring、配置、绑定表和 provider 里。**框架不含任何具体项目名、机型名、IO 点位表、
报警码目录或云物模型表**——这是硬约束，由 `scripts/check_arch_boundary.sh` 检查。

## 先看哪份文档

| 想做的事 | 去哪 |
|----------|------|
| 找某个符号在哪定义、谁发布谁订阅 | `doc/ai/符号索引.md` |
| 知道一处改动要连带改哪些文件 | `doc/ai/任务索引.md` |
| 找某个目录属于哪个构建目标、有无文档 | `doc/ai/代码地图.md` |
| 理解为什么这样分层、这样取舍 | `doc/architecture/` |
| 查某模块代码级契约、数据模型、启动顺序 | `doc/module-design/` |
| 查某条行为由谁保证、在哪一层验证 | `doc/contract/行为契约.md` |

`doc/ai/` 下前两份表自动生成（`scripts/gen_doc_index.py`），改代码后须重新生成，
否则门禁失败。

## 提交前必跑

```bash
./scripts/check_all.sh              # 五项门禁：架构边界 / 行为契约 / 文档引用 / 格式 / 测试
./scripts/check_all.sh --skip-tests # 无 cmake 环境时只做静态检查
```

报告在 `build-check/test-results/report.html`。

## 容易漏的登记点

框架的硬约束大量落在登记点上。漏掉时症状不是编译失败，而是门禁在另一个位置报一条
看起来无关的错。完整清单见 `doc/ai/任务索引.md`，最常踩的几处：

| 改动 | 必须同时做 |
|------|-----------|
| 新增框架 `.c` | 登记到 `cmake/wdf_targets.cmake`（否则 R7 失败） |
| 新增 `.h` | 保护宏 = 路径全大写下划线形式（否则 R15 失败） |
| 新增 vendor provider | 根 `CMakeLists.txt` 加 `WDF_ENABLE_*` 选项 + `check_arch_boundary.sh` 的 include 白名单 |
| 新增持锁文件 | 在 `check_arch_boundary.sh` 登记 RT 可达性分类（R16，未登记即报错） |
| 新增阻塞等待 | 在 R18 的有界/无界/定时三张表之一登记 |
| 调容量常量 | 同步改常量注释的实测依据 + 对应测试断言 + `architecture/08` 第 19.3 节表格 |
| 改文档引用的路径或符号 | 必须真实存在（否则 D1/D2 失败） |

## 分层与依赖方向

```text
adapters / demo / 项目 wiring
        ↓
ports / application
        ↓
domain
        ↓
common

runtime 负责启动与调度，可调各层 init，不承载业务规则。
observability 是旁路设施，只依赖 common，不回调任何层。
```

`domain/` 另有四条层内禁令：不做文件 IO、不解析序列化格式、不自建线程或周期任务、
不使用动态内存（方案引擎三文件已登记豁免）。各层允许的出向依赖见
`doc/ai/任务索引.md`，规则实现见 `scripts/check_arch_boundary.sh`。

## 约定

- 注释、文档、提交消息用中文。
- 头文件带 doxygen `@brief`（当前 79 个框架头文件覆盖率 100%，符号索引从此抽取）。
- 提交消息格式 `[type]: 中文摘要`，type 取 `feat` / `fix` / `refactor` / `test` /
  `docs` / `tools`；正文按主题分段说清「原先为何有问题」，末尾一行 `验证：` 列具体数字。
- 格式化用 `./scripts/format.sh`（配置在 `.clang-format`）。
- 注意行尾：部分文档是 CRLF，改动时须保留，否则会产生全文件 diff 掩盖真实改动。
