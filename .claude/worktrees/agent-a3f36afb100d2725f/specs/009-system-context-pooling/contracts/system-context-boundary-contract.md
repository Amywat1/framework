# Contract：`system_context` 公开边界与集成约束

## 目的

确保 `system_context_t` 对外始终是组合根资源，而不是调用方可自带存储的普通结构体。

## 契约规则

1. 公开头文件只允许暴露不透明前置声明和正式生命周期/装配/查询接口，不允许暴露真实结构体字段。
2. 私有头文件或内部实现文件可以保留真实 `struct system_context_t`、对象池槽位、空闲链表和测试所需的受控白盒信息。
3. `main` 负责 acquire、装配、运行和 release，但不负责持有对象池实现细节。
4. `controller_scheduler`、`main_loop_*()`、CLI 适配层和文件仓储适配层只消费正式 `system_context_t *`，不拥有创建或销毁策略。
5. 测试辅助必须通过正式生命周期构造实例；只有明确的白盒测试可以包含私有头，而且它们只能验证池语义与边界，不得重新引入外部自带存储模型。
6. CMake 对外公开的 include 目录保持在 `include/`；`src/application/coordinators/` 下的私有边界不作为公开 API 暴露。

## 可验证结果

- 源码树中不再出现新的 `system_context_t local_context;` 正式使用路径。
- `tests/test_support.h` 迁移到统一 acquire / release 辅助入口。
- 白盒测试数量可以存在，但它们断言的是“资源边界是否生效”，而不是“结构体字段是否仍可被外部构造”。
