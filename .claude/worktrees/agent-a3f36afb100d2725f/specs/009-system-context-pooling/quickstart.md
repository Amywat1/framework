# Quickstart：实现 `system_context` 正式生命周期收口

## 1. 理解这次改造的目标

- 目标不是“把堆对象搬到静态区”本身，而是“禁止外部自带 `system_context_t` 存储”。
- 静态预分配对象池、空闲链表头弹出/头插回收和 `in_use` 合法性校验是当前选定的实现路径。
- 公开边界最终只允许：获取句柄、装配端口、运行、重置、释放。

## 2. 先修改公开边界

优先处理这些文件：

- `include/application/coordinators/system_context.h`
- `src/application/coordinators/system_context.c`
- `src/application/coordinators/system_context_private.h`

实施要点：

- 删除“调用方提供存储”的公开语义与说明
- 引入正式 acquire / release 入口
- 保留 reset，但明确它不是 release
- 在内部实现里先把对象池初始化成完整空闲链表
- 让需要状态校验的生命周期入口返回显式结果

## 3. 再迁移使用方

按这个顺序推进最稳妥：

1. `tests/test_support.h` 先改成正式 acquire / release，建立统一测试入口  
2. `src/main/main.c` 改成“申请句柄 -> 装配 -> 运行 -> 释放”  
3. `src/platform/linux/controller_scheduler_linux.c` 与 `main_loop_*()` 对齐新生命周期约束  
4. `src/adapters/**/*` 改为依赖正式句柄，不再假设调用方自带存储  
5. 最后清理白盒测试与旧契约

内部资源管理建议直接按下面的顺序实现：

1. 静态数组提前准备整批 `system_context` 槽位  
2. 初始化空闲链表，把所有槽位串起来  
3. acquire 时只做链表头弹出、槽位清理、`in_use=true`  
4. release 时只做合法性校验、运行态清理、头插回链表、`in_use=false`  
5. reset 不改链表，只改运行态

## 4. 构建与回归

```powershell
cmake -S . -B build
cmake --build build -j2
cd build
ctest --output-on-failure
```

建议先跑这些聚焦测试：

```powershell
ctest --output-on-failure -R "test_contract_system_context_boundary|test_unit_runtime_object_ownership|test_integration_formal_entry_ownership|test_contract_main_loop_boundary|test_contract_controller_scheduler"
```

## 5. 完成判定

满足以下条件后，再进入 `/speckit-tasks`：

- 公开头文件不再允许外部自带 `system_context_t` 存储
- `owns_storage` 及其相关分支被删除
- `main`、调度器、适配层、测试统一走正式生命周期
- 资源不可用、重复释放、非法句柄、重置与释放分离都有回归验证
- 对象池实现已经明确采用“预分配 + 头弹出 + 头插回收 + `in_use` 仅校验”的单一策略
