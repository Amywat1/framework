# 契约：启动失败回滚

## 目标

把启动阶段的失败路径固定为显式错误返回 + 统一逆序回滚，避免半初始化对象残留。

## 覆盖失败场景

- `system_context` 获取失败
- 同一进程内已有正式 runtime 实例尚未销毁
- 必需配置缺失
- 端口未绑定
- `file_program_repository_init(...)` 失败
- `file_event_logger_init(...)` 失败
- `controller_scheduler_linux_create(...)` 失败

## 回滚契约

- 任一阶段失败后，create 必须返回明确 `operation_result_t`。
- 任一阶段失败后，已成功创建的 runtime owned 资源必须全部清理。
- 回滚顺序必须与创建顺序相反。
- 已绑定但不归 runtime 所有的 caller owned 资源只解除关联，不执行销毁。
- 回滚完成后，调用方必须可以立即再次执行新的 create。
- 若失败原因是“已有正式实例未销毁”，则不得触发现有实例的回滚或状态改变。

## 验证重点

- 同一进程内，连续“失败 create -> 成功 create”必须成立。
- 已有正式实例存活时，第二次 create 必须明确失败且不影响现有实例。
- 失败后旧 runtime 句柄不能继续表现为有效 scheduler 绑定。
- 失败路径不得依赖调用方手工补清理。
