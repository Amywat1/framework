# Phase 0 研究：system_context 组合根生命周期收口

## 决策 1：公开生命周期统一为 acquire / reset / release，移除外部自带存储语义

- Decision：用一套正式生命周期替代当前 `create + initialize + destroy` 混合语义。公开边界以“获取正式实例、装配端口、运行、重置、释放”为唯一模型，不再允许调用方提供 `system_context_t` 存储。
- Rationale：当前头文件同时支持堆分配对象和调用方自带存储，导致 `owns_storage`、重置与释放语义相互纠缠。统一入口后，主程序、调度器、适配层和测试都能共享同一套拥有权模型。
- Alternatives considered：
  - 保留 `system_context_initialize()` 作为外部自带存储入口：被拒绝，违背本次边界收口目标。
  - 只保留 `malloc/free` 路径：被拒绝，虽然消除了外部存储，但仍保留不稳定的动态分配依赖。

## 决策 2：公开头文件保持不透明类型，私有实现持有真实结构体与池状态

- Decision：`include/application/coordinators/system_context.h` 只保留不透明前置声明和正式接口；真实 `struct system_context_t`、槽位元数据、空闲链表和资源状态仅保留在 `src/application/coordinators/` 私有边界。
- Rationale：这次改造最关键的不是“内存在哪”，而是“谁拥有对象存储”。把真实结构体留在私有边界，才能允许后续继续调整字段布局和生命周期约束，而不再被白盒外部使用方式绑住。
- Alternatives considered：
  - 继续让测试和适配层默认包含私有头：被拒绝，白盒测试会把旧对象语义重新拉回公开边界。
  - 在公开头里保留完整结构体定义，仅新增 acquire/release：被拒绝，外部仍可绕开正式生命周期。

## 决策 3：内部实现使用静态预分配对象 + 空闲链表头弹出/头插回收 + `in_use` 合法性校验

- Decision：内部提前静态准备一批 `system_context` 对象，空闲链表只维护“当前可分配节点”；申请时从链表头拿一个节点，释放时把节点重新挂回链表头；`in_use` 不参与分配流程，只负责校验句柄当前是否合法、是否已占用。
- Rationale：这条路径满足用户给定技术栈，不引入新语言、第三方对象池库或额外分配器，同时能把对象来源完全收回到内部。链表头弹出/头插回收实现简单直接，获取/释放保持 O(1)，而 `in_use` 仅作合法性校验可以避免把资源分配逻辑分散到多套状态判断里。
- Alternatives considered：
  - 第三方对象池或自定义分配器：被拒绝，超出最小范围。
  - 只用静态数组不维护空闲链表：被拒绝，释放后复用与资源不可用语义会变得脆弱。
  - 让 `in_use` 同时承担“是否在空闲链表中”的分配职责：被拒绝，会让资源来源与合法性判断混在一起，增加重复释放与回收路径的脆弱性。

## 决策 4：重置保留装配关系，释放结束占用关系

- Decision：`reset` 只清空运行态与序列状态，保留已经装配的 `sensor_port`、`actuator_port`、`event_logger_port`、`program_repository_port`；`release` 清空运行态、解除占用并把槽位归还空闲链表。
- Rationale：现有 `system_context_reset()` 已经体现“清空运行态但保留端口”的行为，这与主控重装配前复位的需求一致。把这种语义正式化，可以避免主程序和测试误把 reset 当成 destroy。
- Alternatives considered：
  - reset 同时释放端口绑定：被拒绝，会把主流程重置路径与资源结束路径再次混在一起。
  - release 仅标记失效不清空状态：被拒绝，会增加复用泄漏风险。

## 决策 5：生命周期异常语义对外正式化，旧指针复用被视为拥有者违规

- Decision：对空指针、非池内句柄、槽位未占用、资源不可用、重复释放和无效重置返回显式错误。调用方在 release 之后继续持有并复用旧 `system_context_t *` 被定义为边界违规；实现通过池成员校验和 `in_use` 捕获可检测场景。
- Rationale：在保持 `system_context_t *` 这一现有调用形态的前提下，最小代价方案是把正式错误边界集中在“可验证的拥有权状态”上，而不是为彻底解决旧指针重租赁识别问题引入更大 API 破坏。
- Alternatives considered：
  - 改成 value-handle 或双对象 lease token：被拒绝，公开 API 破坏面过大，不符合本轮最小增量。
  - 完全不定义 release 后误用：被拒绝，会让异常路径继续模糊。

## 决策 6：测试辅助迁移到正式获取路径，白盒断言只验证池与边界

- Decision：`tests/test_support.h` 和现有 unit / contract / integration / simulation 测试统一改成先获取正式 `system_context` 句柄，再执行装配和运行；必要白盒测试只验证池占用、释放、资源不可用和边界隔离。
- Rationale：当前大量测试直接在栈上声明 `system_context_t system_context;`，这是旧语义最大的拖拽点。先把测试迁走，后续实现才不会被回归套牢。
- Alternatives considered：
  - 保留旧测试写法，只改生产代码：被拒绝，会形成正式路径与测试路径双轨。
  - 完全移除白盒测试：被拒绝，会损失对内部边界回归的把控。
