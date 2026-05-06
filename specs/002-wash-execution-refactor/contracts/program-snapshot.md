# 接口契约：程序快照与启动校验

## 1. 说明

本契约定义启动前程序数据校验与会话内程序快照冻结的逻辑语义，供程序仓储适配器、启动用例和验证测试共同遵循。

## 2. 启动前校验输入

| 字段 | 类型 | 说明 |
|------|------|------|
| program_id | string | 请求启动的程序标识 |
| requested_at | datetime | 启动请求时间 |

## 3. 校验结果输出

| 字段 | 类型 | 说明 |
|------|------|------|
| validation_result | enum | valid / unavailable / invalid |
| source_revision | integer | 当前程序版本 |
| reject_reason | enum | none / program_unavailable / program_invalid |
| snapshot_payload_ref | string | 生成的快照引用，若校验通过 |
| program_snapshot_id | string | 绑定到新会话的快照标识，若校验通过 |

## 4. 快照绑定语义

- 只有 `validation_result=valid` 时，才允许创建会话。
- 新会话必须绑定一个独立的 `program_snapshot_id`。
- 会话运行期间，即使外部程序配置被修改，当前会话仍必须继续引用原快照。
- 新的程序配置只影响后续新的启动请求。

## 5. 契约约束

- 不允许“先创建会话，后校验失败再中止”的路径。
- 快照一旦绑定到会话，不得被原地替换。
- 校验失败必须返回显式拒绝原因，并生成可追踪记录。
- 会话运行期间若外部程序被更新，当前会话的 `program_snapshot_id` 必须保持不变。
