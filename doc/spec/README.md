# 行为规格驱动开发（Spec-Driven Development）

## 目的

用结构化规格定义"什么是对的"，用对抗式验证证明"确实做对了"。

前半段（需求迭代 → 规格生成 → 归档追踪）由 OpenSpec 管理，
后半段（对抗测试 → 门禁验证）由本项目自建。

## 目录结构

```
doc/spec/
├── README.md              ← 本文件
├── _adversarial.md        ← 对抗测试 prompt 模板（OpenSpec 不提供）
└── _workflow.md           ← 完整工作流说明

openspec/
├── changes/               ← 进行中的功能变更（OpenSpec 管理）
│   └── <feature-name>/
│       ├── proposal.md
│       ├── specs/<cap>/spec.md   ← 行为规格（含时序/不变量/状态机）
│       ├── design.md
│       └── tasks.md
├── specs/                 ← 归档后的主规格目录（系统当前行为的真相）
└── schemas/
    └── spec-driven-custom/       ← 嵌入式定制 schema（扩展了时序/不变量/故障注入）
```

## 快速入口

| 我要做什么 | 操作 |
|------------|------|
| 开始一个新功能 | `/opsx:propose "描述"` 或先 `/opsx:explore` |
| 实现完后做对抗测试 | `/opsx:adversarial` 或手动用 `_adversarial.md` |
| 了解完整工作流 | 看 `_workflow.md` |
| 实现并验证 | `/opsx:apply` |
| 功能完成归档 | `/opsx:archive` |

## 与现有文档体系的关系

| 文档 | 定位 | 生命周期 |
|------|------|----------|
| `doc/architecture/` | 为什么这样分层 | 长期稳定 |
| `doc/module-design/` | 代码级结构与契约 | 随模块演进 |
| `doc/contract/行为契约.md` | 框架对全体项目的承诺 | 极少变更 |
| `openspec/specs/` | 各功能的完整行为规格 | 随 archive 更新 |
| `openspec/changes/<name>/` | 单次功能开发的工作区 | 完成后归档 |

规格归档后，其中的框架级不变量应提炼进 `行为契约.md`，
模块内部约束进入对应 `module-design/` 文件。
