# 洗车机嵌入式软件 — 领域设计文档

> **版本**: v0.7
> **状态**: 草稿（代码架构设计完成，Codex 终审修订完成）
> **说明**: 本文档涵盖 DDD 领域分析（战略设计 + 战术设计）及代码架构设计，是编码实现的直接依据。

---

## 0. 当前代码对齐说明

本文件包含较多前期草稿描述，当前编码、联调、评审请优先以以下内容为准：

- `src/` 中实际实现优先于本文旧草稿
- 本文中标注“当前实现优先规则（2026-04-27）”的章节优先于历史草稿正文

特别说明：

- 当前不再发布 `WashSessionStarted`、`PassStarted`、`PassCompleted`
- 服务完成评估不再通过 `PostWashAssessmentCompleted` 事件传播
- `Recover` 不再通过 `RecoveryRequested` / `RecoveryCompleted` 事件编排
- 指令拒绝仅通过 `command_result_t` 返回，不再发布 `CommandRejected`

当前 demo 最小验证入口：

- `./carwash`
- `./carwash comm_timeout`
- `./carwash travel_timeout`
- `./carwash recover`

---

## 目录

1. [项目背景](#1-项目背景)
2. [核心服务原则](#2-核心服务原则)
3. [统一语言](#3-统一语言)
4. [子域划分](#4-子域划分)
5. [限界上下文](#5-限界上下文)
6. [上下文地图](#6-上下文地图)
7. [业务规则](#7-业务规则)
8. [核心领域模型](#8-核心领域模型)
9. [领域事件清单](#9-领域事件清单)
10. [运行模式状态机](#10-运行模式状态机)
11. [报警体系](#11-报警体系)
12. [WashSession 聚合详细设计](#12-washsession-聚合详细设计)
13. [OperationalMode 聚合详细设计](#13-operationalmode-聚合详细设计)
14. [AlarmRegistry 聚合详细设计](#14-alarmregistry-聚合详细设计)
15. [代码架构设计](#15-代码架构设计)

---

## 1. 项目背景

基于 Linux 系统开发的洗车机嵌入式软件。设备包含侧刷、顶刷、龙门、风机等执行机构，配合多种传感器完成自动洗车流程。

**技术约束**：

- 开发语言：C
- 运行平台：Linux 嵌入式系统
- 硬件通信协议：CAN、GPIO、Modbus、UART
- 云平台：阿里云、AWS、涂鸦（需同时支持，可切换）
- 功能要求：洗车控制、云端物模型上报/接收、OTA 升级、报警管理、安全监控、自检

---

## 2. 核心服务原则

**最大服务完成原则（Maximum Service Completion Principle）**

> 在安全许可的前提下，系统优先保障当前洗车会话完整完成，而不是因非严重故障提前中止服务。故障处置决策在服务边界（每次洗车会话结束时）做出，而不是在故障发生时立即做出。

此原则直接决定 MAJOR 级报警的处置策略，是整个报警体系设计的出发点。

---

## 3. 统一语言

以下词汇在代码、文档、沟通中保持完全一致。


| 中文       | 领域术语                        | 说明                                                                                |
| -------- | --------------------------- | --------------------------------------------------------------------------------- |
| 洗车程序     | WashProgram                 | 定义一次完整洗车的执行方案，包含趟序列和所有参数配置                                                        |
| 洗车会话     | WashSession                 | 一次完整洗车过程的运行实例，有完整的生命周期                                                            |
| 趟        | Pass                        | 洗车程序的基本执行单元，完成从头到尾的一遍完整动作；内含有序的运动序列，可包含移动、停止等阶段；机构和药剂通过位置触发或时间触发激活                |
| 运动步骤     | MovementStep                | 趟内部的单个运动指令，类型为 MOVE（移动到目标位置）或 STOP（静止等待指定时长）                                      |
| 位置触发     | PositionTrigger             | 龙门位置进入指定区间 [start_mm, stop_mm] 时激活机构/药剂的触发条件，在 MOVE 阶段评估                          |
| 时间触发     | TimeTrigger                 | 龙门停止后计时进入指定区间 [start_ms, stop_ms] 时激活机构/药剂的触发条件；计时基准为本次 STOP 指令开始时刻，在 STOP 阶段评估   |
| 车辆画像     | VehicleProfile              | 当前洗车会话中在线识别出的车辆参数模型，包含车头/车尾边界、车长、车高/车宽类别、轮廓采样和识别置信度                               |
| 轮廓采集     | ProfileAcquisition          | 在龙门运动过程中持续采集传感器数据、识别车辆前后边界与轮廓，并逐步更新 VehicleProfile 的过程                            |
| 运行时计划    | RuntimeWashPlan             | 基于 WashProgram 模板和 VehicleProfile 实时生成的本次会话执行计划，决定本车的具体激活窗口与降级策略                  |
| 龙门绝对位置触发 | GantryPositionTrigger       | 以龙门轨道绝对位置为参考坐标的触发条件，用于粗窗口、安全包络和不依赖车长的动作                                           |
| 车辆相对位置触发 | VehiclePositionTrigger      | 以车头/车尾/车中心为锚点的触发条件，用于相对车辆边界的精准动作                                                  |
| 车辆比例触发   | VehicleRatioTrigger         | 以整车长度百分比为参考的触发条件，用于适配不同车长车型的泛化动作                                                  |
| 执行机构     | Actuator                    | 接受控制指令的物理设备（侧刷、顶刷、风机、水泵等）                                                         |
| 接触性执行机构  | ContactActuator             | 需要与车身产生物理接触或近距离贴合的执行机构，如侧刷、顶刷等；在车辆画像未锁定时默认按高风险动作处理                                |
| 龙门       | Gantry                      | 特殊执行机构，承载刷具做往返运动，具有位置概念                                                           |
| 传感器      | Sensor                      | 采集物理量的设备（压力、位置、电流、温度等）                                                            |
| 药剂       | Chemical                    | 洗车过程中喷射的液体（泡沫剂、蜡剂、清洁剂等）                                                           |
| 报警       | Alarm                       | 系统检测到的异常状态，有等级和处置策略                                                               |
| 报警定义     | AlarmDefinition             | 静态配置，描述一种报警的等级、清除方式和处置策略                                                          |
| 自清除报警    | Self-Clearing Alarm         | 在静止状态可持续检测，条件恢复后自动清除的报警                                                           |
| 运动触发报警   | Motion-Triggered Alarm      | 在静止状态无法持续评估的报警；相关机构被驱动时（任何模式），系统自动重新评估故障条件，条件消除则自动清除                              |
| 服务完成评估   | Post-Wash Assessment        | 洗车会话结束后，评估活跃报警是否阻断下次服务的过程                                                         |
| 急停       | EStop                       | 立即停止所有执行机构的安全机制                                                                   |
| 自检       | SelfCheck                   | 主动驱动每个执行机构动作，全面验证设备状态的诊断过程                                                        |
| 持续监控     | ContinuousMonitoring        | 后台持续采集传感器数据、检测异常的旁路监视过程                                                           |
| 运行模式     | OperationalMode             | 系统当前所处的工作状态，同一时刻只能处于一种模式                                                          |
| 物模型点位    | ThingProperty               | IoT 云平台的设备属性/事件数据点定义                                                              |
| 防腐层      | ACL (Anti-Corruption Layer) | 隔离外部系统（云平台）私有协议与领域模型的翻译层                                                          |
| 安全状态     | SafeState                   | 龙门停在当前位置不动、其余执行机构到达安全位置的设备状态（刷具停止并抬起、所有泵/阀/风机关闭）                                  |
| 恢复操作     | Recover                     | 操作员主动触发的、将设备从停机状态恢复到待机状态的操作；执行时清除所有报警并驱动其余机构到安全位置（龙门停在原地）；唯一前提：急停报警未激活            |
| 恢复中      | Recovering                  | 执行 Recover 操作期间的过渡状态；并行驱动其余执行机构到安全位置（龙门停在当前位置不动），全部到位则进入 IDLE，超时有未到位则进入 EXCEPTION |


---

## 4. 子域划分


| 子域   | 类型      | 说明                       |
| ---- | ------- | ------------------------ |
| 洗车执行 | **核心域** | 产品的核心价值所在，定义洗车程序和会话的完整行为 |
| 安全管理 | **支撑域** | 报警检测、安全监控、自检，保障设备安全运行    |
| 设备控制 | **支撑域** | 抽象硬件执行机构和传感器，屏蔽协议差异      |
| 指令网关 | **支撑域** | 管理运行模式，仲裁所有外部指令的合法性      |
| 云端接入 | **通用域** | 多云平台适配、物模型上报/接收、OTA，可替换  |
| 维护调试 | **通用域** | 调试 Shell、日志管理、诊断信息，只读观察  |


---

## 5. 限界上下文

### 5.1 洗车执行上下文（Wash Execution）— 核心域

**职责**：管理洗车程序定义，执行洗车会话，驱动趟序列，响应报警事件。

**包含概念**：WashSession、WashProgram、Pass、Chemical

**不包含**：具体硬件控制逻辑（通过接口依赖设备控制上下文）

---

### 5.2 设备控制上下文（Device Control）— 支撑域

**职责**：将洗车执行上下文的抽象指令翻译为具体硬件操作，持续发布传感器数据。

**包含概念**：Actuator、Sensor、Gantry、DeviceCommand

**对外接口（由洗车执行上下文定义）**：

- `IActuatorPort`：执行机构控制
- `IGantryPort`：龙门位置控制
- `ISensorPort`：传感器读取

**实现方式**：GPIO / CAN / Modbus / UART 适配器

---

### 5.3 安全管理上下文（Safety Management）— 支撑域

**职责**：订阅传感器数据，对照报警定义表检测异常，发布报警事件，管理报警生命周期，执行服务完成评估。

**包含概念**：Alarm、AlarmDefinition、AlarmRegistry、PostWashAssessment

**两种工作形态**：

- **持续监控**（ContinuousMonitoring）：后台旁路进程，始终运行，不是独立运行模式
- **完整自检**（SelfCheck）：独立运行模式，主动驱动所有机构验证状态

---

### 5.4 指令网关上下文（Command Gateway）— 支撑域

**职责**：持有唯一运行模式状态，对所有外部指令做合法性校验，路由合法指令到目标上下文。

**包含概念**：OperationalMode、CommandRequest、CommandResult

**所有外部指令必须经过此上下文**，不允许绕过。

---

### 5.5 云端接入上下文（Cloud IoT）— 通用域

**职责**：管理与云平台的连接，上报物模型点位，接收云端下发指令，处理 OTA 升级。

**包含概念**：ThingProperty、CloudCommand、OtaTask

**关键设计**：每个云平台（阿里云、AWS、涂鸦）有独立的 ACL 实现，对上层暴露统一接口。

---

### 5.6 维护调试上下文（Maintenance）— 通用域

**职责**：提供调试 Shell 命令行接口，聚合所有上下文的日志和事件，支持远程诊断。

**特点**：只读订阅所有上下文事件，调试写操作（手动控制机构）需经过指令网关仲裁。

---

## 6. 上下文地图

### 6.1 关系总览

```
                     ┌─────────────────────┐
  阿里云/AWS/涂鸦 ──→ │    云端接入上下文    │
                     │  [ACL 防腐层在此]    │
                     └──────────┬──────────┘
                                │ 翻译后的领域指令
                                ▼
┌──────────────┐      ┌─────────────────────┐
│  本地 UI/按钮 │ ───→ │    指令网关上下文    │ ← 运行模式仲裁器
│  调试 Shell  │      └──┬──────┬───────┬───┘
└──────────────┘         │      │       │
            ┌────────────┘      │       └────────────┐
            ▼                   ▼                     ▼
┌───────────────────┐  ┌─────────────────┐  ┌──────────────────┐
│  洗车执行上下文    │  │  安全管理上下文  │  │  设备控制上下文   │
│   [核心域]        │  │   [支撑域]       │  │   [支撑域]        │
└────────┬──────────┘  └────────┬────────┘  └──────┬───────────┘
         │ ① 执行指令            │ ③ 报警事件        │ ② 传感器数据
         └───────────────────────┴──────────────────┘

┌─────────────────────────────────────────────┐
│          维护调试上下文（被动订阅所有事件）    │
└─────────────────────────────────────────────┘
```

### 6.2 集成模式说明


| 关系  | 上游    | 下游      | 集成模式     | 说明                 |
| --- | ----- | ------- | -------- | ------------------ |
| 关系① | 洗车执行  | 设备控制    | 客户—供应商   | 接口由洗车执行定义，设备控制实现   |
| 关系② | 设备控制  | 安全管理    | 发布语言     | 设备控制发布传感器事件，安全管理订阅 |
| 关系③ | 安全管理  | 洗车执行/网关 | 发布语言     | 安全管理发布报警事件，各方订阅响应  |
| 关系④ | 云端接入  | 指令网关    | 防腐层（ACL） | 云平台私有协议翻译为领域指令     |
| 关系⑤ | 所有上下文 | 维护调试    | 开放主机服务   | 所有事件向公共日志流发布       |


### 6.3 防腐层（ACL）翻译示例


| 云端物模型（输入）                   | 领域指令（输出）                                             |
| --------------------------- | ---------------------------------------------------- |
| `wash_mode = 2`             | `StartWashCommand { program_id: "standard" }`        |
| `manual_ctrl_pump = 1`      | `ManualActuatorCommand { id: PUMP_MAIN, state: ON }` |
| `self_check_trigger = true` | `StartSelfCheckCommand {}`                           |
| `ota_confirm = true`        | `OtaApplyCommand { version: "1.2.3" }`               |


---

## 7. 业务规则

### 7.1 洗车执行规则


| 编号    | 规则描述                                                                                     |
| ----- | ---------------------------------------------------------------------------------------- |
| BR-01 | 启动洗车指令到达时，运行模式必须是 IDLE，否则拒绝                                                              |
| BR-04 | 同一时刻只能有一个活跃的洗车会话                                                                         |
| BR-11 | 不同洗车程序之间无继承关系，每个程序是独立的趟序列与药剂组合配置                                                         |
| BR-17 | 程序参数修改仅对下次会话生效；WashSession 创建时对程序定义做深拷贝快照，运行中的会话不受外部程序修改影响                               |
| BR-21 | 洗车会话启动时，`VehicleProfile.status = UNKNOWN`；第一趟允许边探测边执行低风险动作                               |
| BR-22 | 所有依赖车辆相对位置或车辆比例的触发条件，必须在 `VehicleProfile` 达到要求状态后才可评估为 true                              |
| BR-23 | 在 `VehicleProfile` 未 `FULL_LOCKED` 前，侧刷、顶刷等 `ContactActuator` 默认禁止激活，除非程序显式配置保守 fallback |
| BR-24 | `VehicleProfile` 识别失败时，系统按 `FallbackPolicy` 执行：安全降级、退回通用窗口或中止会话                          |
| BR-25 | 车辆轮廓识别结果仅对当前 `WashSession` 有效，不影响其他会话                                                    |
| BR-26 | 同一会话内 `RuntimeWashPlan` 可随 `VehicleProfile` 状态升级而增量更新，但已执行动作不回滚                          |


### 7.2 报警与安全规则


| 编号    | 规则描述                                                                            |
| ----- | ------------------------------------------------------------------------------- |
| BR-02 | CRITICAL 报警触发时，立即停止所有执行机构，系统进入 EXCEPTION 模式                                     |
| BR-03 | MAJOR 报警触发时，当前洗车会话继续完成（最大服务完成原则），会话结束后执行服务完成评估                                  |
| BR-10 | MINOR 报警不影响任何运行状态，仅记录和上报                                                        |
| BR-12 | 自清除报警（AUTO_STATIC）：检测条件可持续监测，条件恢复时自动清除                                          |
| BR-13 | 运动触发清除报警（ON_MOTION）：相关机构被驱动时（任何模式均可），系统自动重新评估报警条件，条件消除则自动清除；或由 Recover 操作统一批量清除 |
| BR-14 | 服务完成评估：会话结束后若存在 `blocks_next_service = true` 的活跃报警，系统进入 EXCEPTION，而非停留在 IDLE    |
| BR-18 | Recover 操作执行时清除所有活跃报警，唯一前提：急停报警（EStop）未激活                                       |
| BR-19 | RECOVERING 状态下并行驱动其余执行机构到安全位置（龙门停在当前位置不动）：全部到位则进入 IDLE；超时仍有机构未到位则进入 EXCEPTION   |


### 7.3 运行模式规则


| 编号    | 规则描述                                                              |
| ----- | ----------------------------------------------------------------- |
| BR-05 | MANUAL 模式下，洗车启动指令被拒绝                                              |
| BR-06 | WASHING 模式下，单机构手动控制指令被拒绝                                          |
| BR-07 | MANUAL 和 EXCEPTION 模式下，允许手动操作机构（ManualActuatorCommand）和执行 Recover |
| BR-08 | EXCEPTION 模式下，EStop 激活时 RecoverCommand 被拒绝；EStop 清除后方可执行          |
| BR-09 | SELF_CHECK 模式下，洗车启动指令和手动控制指令均被拒绝                                  |
| BR-15 | MANUAL 和 EXCEPTION 模式下均无超时自动退出机制，必须由操作员显式执行 Recover               |
| BR-16 | 洗车启动和完整自检均可由云端远程触发，经指令网关仲裁后执行                                     |
| BR-20 | RECOVERING 期间所有外部指令被拒绝，直至恢复完成                                     |


---

## 8. 核心领域模型

### 8.1 洗车程序（WashProgram）

```
WashProgram:
  id       : 程序唯一标识
  name     : 程序名称（"快洗" / "标准洗" / "精洗" / 自定义）
  passes[] : 趟配置序列（有序）
  fallback_policy : CONTINUE_SAFE_ONLY | SKIP_RISKY_ACTUATORS | USE_GENERIC_WINDOW | ABORT_SESSION

WashPass（趟）:
  index            : 趟序号（从 0 开始）
  movement_steps[] : 运动序列（有序，定义龙门的完整行为）
    MOVE { target_position_mm, speed_mm_s }   ← 移动到目标位置
    STOP { duration_ms }                      ← 静止等待指定时长

  actuator_triggers[]: 执行机构触发配置（无序）
    { actuator_id, activation_policy }

  chemical_triggers[]: 药剂触发配置（无序）
    { chemical_id, flow_rate, activation_policy }

ActivationPolicy（动作激活策略）:
  all_of[] : 必须全部满足
  any_of[] : 任一满足
  not_of[] : 必须不满足
  confidence_required : 触发所需最低轮廓识别置信度
  fallback_policy     : 本动作专用降级策略（可选）

TriggerCondition（触发条件，每个触发器为以下类型之一）:
  GANTRY_POSITION { start_mm, stop_mm }
    → 龙门位置满足 min(start_mm,stop_mm) ≤ gantry_pos_mm ≤ max(start_mm,stop_mm) 时激活
    → 自动归一化：start/stop 大小顺序不影响语义
    → MOVE 阶段评估，以 gantry_pos_mm 判断

  VEHICLE_POSITION { anchor: FRONT | REAR | CENTER, start_mm, stop_mm }
    → 以车辆前/后/中心锚点为参考，解析为本次车辆对应的绝对轨道位置窗口
    → 仅当 VehicleProfile 至少已锁定相应锚点时才可评估

  VEHICLE_RATIO { start_pct, stop_pct }
    → 以整车长度比例作为触发区间，如 5%~95% 表示车身主体区
    → 仅当 VehicleProfile.status = FULL_LOCKED 时才可评估

  TIME { start_ms, stop_ms }
    → 龙门停止后计时在区间内时激活
    → STOP 阶段评估，计时基准为本次 STOP 开始时刻

  PROFILE_STATE { required_status: FRONT_LOCKED | PARTIAL_LOCKED | FULL_LOCKED }
    → 仅当车辆画像达到指定锁定状态时才允许该动作激活

注：
  · WashProgram 是“洗车意图模板”，不直接绑定某一辆车的最终绝对位置
  · 同一机构/药剂可配置多个触发器；动作是否激活由 ActivationPolicy 组合求值
  · GANTRY_POSITION 适合粗窗口/安全包络；VEHICLE_POSITION / VEHICLE_RATIO 适合基于本车轮廓的精准动作
```

**程序配置示例**：

```
快洗程序（3趟）:
  趟0:
    运动：MOVE { target: 5000mm, speed: 600mm/s }
    触发：
      清水：all_of = [GANTRY_POSITION[0, 5000]]
      泡沫：any_of = [GANTRY_POSITION[200, 4800], VEHICLE_RATIO[0, 100]]
            fallback = USE_GENERIC_WINDOW

  趟1:
    运动：MOVE { target: 0mm, speed: 600mm/s }
    触发：
      侧刷：all_of = [PROFILE_STATE(FULL_LOCKED), GANTRY_POSITION[200, 4800], VEHICLE_RATIO[5, 95]]
      清水：all_of = [GANTRY_POSITION[0, 5000]]

  趟2:
    运动：MOVE { target: 5000mm, speed: 400mm/s }
    触发：
      风机：all_of = [PROFILE_STATE(FULL_LOCKED), VEHICLE_RATIO[-5, 105]]

精洗程序（含停留浸泡）趟1示例:
  运动：
    MOVE { target: 5000mm, speed: 400mm/s }
    STOP { duration: 30000ms }              ← 停留30秒浸泡
  触发：
    侧刷：
      all_of = [PROFILE_STATE(FULL_LOCKED), VEHICLE_RATIO { start_pct: 5, stop_pct: 95 }]
    顶刷：
      all_of = [PROFILE_STATE(FULL_LOCKED), VEHICLE_POSITION { anchor: FRONT, start_mm: 300, stop_mm: 4300 }]
    泡沫剂A：
      any_of = [GANTRY_POSITION { start_mm: 500, stop_mm: 4500 },
                VEHICLE_RATIO { start_pct: 0, stop_pct: 100 }]
      TIME   = { start_ms: 0, stop_ms: 30000 } ← 停止后全程浸泡
```

---

### 8.2 洗车会话（WashSession）

```
WashSession（聚合根）:
  session_id       : 会话唯一标识
  program_snapshot : 洗车程序快照（创建时深拷贝，与外部程序定义隔离，见 BR-17）
  state            : 会话状态（见第12节状态机）
  current_pass_idx : 当前执行的趟序号
  vehicle_profile  : 本次车辆画像（前/后边界、车长、轮廓采样、识别状态、置信度）
  runtime_plan     : 基于 program_snapshot + vehicle_profile 生成的会话级运行时计划
  executor         : PassExecutor（值内嵌，非指针，零动态分配；每趟复用）
  major_alarm_journal[] : 本次会话期间出现过的 MAJOR 报警码（append-only，唯一集合）
                          · AlarmTriggered(MAJOR) 时追加 alarm_code
                          · 追加前检查去重：同一 alarm_code 只记录一次，多次触发忽略
                          · 不删除（不跟踪清除状态，只记录"曾出现过"）
                          · 仅供 PostWashAssessment 交叉查询用，不负责上报
  abort_cause      : CRITICAL_ALARM | MANUAL | STEP_TIMEOUT（中止时记录）
  started_at       : 会话开始时间
  completed_at     : 会话完成时间（可空）
  domain_events[]  : 待发布的领域事件队列

会话状态：
  CREATED → RUNNING → COMPLETING → COMPLETED
                    ↘ ABORTING → ABORTED

VehicleProfile:
  status           : UNKNOWN | ACQUIRING | PARTIAL_LOCKED | FULL_LOCKED | FAILED
  front_edge_mm    : 车辆前边界对应的龙门轨道位置
  rear_edge_mm     : 车辆后边界对应的龙门轨道位置
  length_mm        : rear_edge_mm - front_edge_mm
  roof_samples[]   : 车顶轮廓采样
  max_height_mm    : 最大车高
  width_class      : NARROW | NORMAL | WIDE | UNKNOWN
  confidence       : 0..100

RuntimeWashPlan:
  source_program_id       : 来源程序
  vehicle_profile_version : 生成计划时使用的画像版本
  pass_plans[]            : 每趟的解析后动作窗口
  fallback_policy         : 当前会话生效的降级策略

设计说明：
  · WashProgram 只定义“想怎么洗”的模板
  · WashSession 在运行时根据 VehicleProfile 持续更新 RuntimeWashPlan
  · 第一趟允许边探测边执行低风险动作；后续趟使用更精准的车辆相对触发
```

---

### 8.3 报警定义（AlarmDefinition）

```
AlarmDefinition（静态配置，系统启动时加载）:
  alarm_code           : 报警码（32位，高8位为模块ID，低24位为具体报警）
  level                : CRITICAL | MAJOR | MINOR
  description          : 报警描述
  source_type          : DEVICE | SENSOR | COMM | LOGIC
  clear_method         : AUTO_STATIC | ON_MOTION
  blocks_next_service  : bool（仅 MAJOR 有意义）
  response_strategy    : STOP_IMMEDIATELY | COMPLETE_THEN_ASSESS | LOG_ONLY
```

**报警码分配规则**：

```
高 8 位（模块 ID）:
  0x01 = 洗车执行
  0x02 = 设备控制（传感器/执行机构）
  0x03 = 通信（CAN/Modbus/UART）
  0x04 = 安全逻辑
  0x05 = 云端/OTA

低 24 位（具体报警）:
  由各模块自行定义，在模块内唯一

示例:
  0x020001 = 主水泵水压过低（设备模块，编号01）
  0x020002 = 侧刷电机过流（设备模块，编号02）
  0x030001 = CAN 总线通信中断（通信模块，编号01）
```

---

### 8.4 运行模式（OperationalMode）

```
OperationalMode（枚举）:
  IDLE           : 待机，等待指令
  WASHING        : 洗车会话执行中
  MANUAL         : 手动停机模式，操作员主动进入，可操作单机构，须 Recover 退出
  SELF_CHECK     : 完整自检模式，主动驱动所有机构
  EXCEPTION      : 异常停机模式，被动进入（CRITICAL/EStop/blocking MAJOR）；
                   设备无法洗车，等待维修；可操作单机构，EStop 清除后可执行 Recover
  RECOVERING     : 恢复过渡状态；并行驱动其余机构到安全位置（龙门停在原地），清除所有报警
```

---

## 9. 领域事件清单

> 当前实现优先规则（2026-04-27）
> 本章后续表格仍保留部分早期草案事件名，编码时请以 `src/` 和
> 本文中已标注的当前实现规则为准。当前事件总线只保留：
> `WashSessionCompleted`、`WashSessionAborted`、`AlarmTriggered`、
> `AlarmCleared`、`SelfCheckCompleted`、`OperationalModeChanged`。
> `WashSessionStarted`、`PassStarted`、`PassCompleted`、
> `PostWashAssessmentCompleted`、`RecoveryRequested`、
> `RecoveryCompleted`、`CommandRejected` 已不再作为当前实现依据。

### 洗车执行上下文发布

> 发布者：`wash_session.c`（领域层），由应用层 `event_dispatcher` 每 tick 拉取后送入事件总线。
> 注：WashSessionCreated 由 `command_handler.c` 在创建会话时发布（属于应用层操作，不由聚合内部发出）。


| 事件                   | 携带数据                                            | 触发条件                                                        |
| -------------------- | ----------------------------------------------- | ----------------------------------------------------------- |
| WashSessionCompleted | session_id, session_major_alarms[]              | 收尾完成，会话正常结束                                                 |
| WashSessionAborted   | session_id, abort_cause, session_major_alarms[] | 会话中止（abort_cause 区分 CRITICAL_ALARM / MANUAL / STEP_TIMEOUT） |


### 安全管理上下文发布

> 发布者：`alarm_registry.c`（领域层），由 `event_dispatcher` 拉取后送入事件总线。
> SelfCheckCompleted 由 `self_check_service.c`（应用层）在自检流程结束后发布。
> EStopTriggered 由 `safety_thread.c`（平台层）检测到硬件信号变化后直接发布，不经过聚合。


| 事件                 | 携带数据                             | 触发条件                                                               |
| ------------------ | -------------------------------- | ------------------------------------------------------------------ |
| AlarmTriggered     | alarm_code, level, source_device | 检测到异常                                                              |
| AlarmCleared       | alarm_code, clear_method         | 报警条件解除                                                             |
| SelfCheckCompleted | passed_items[], failed_items[]   | 完整自检结束（发布者：self_check_service）                                     |
| EStopTriggered     | source                           | 急停触发（发布者：safety_thread；硬件停机与模式切换同步直通；EStopTriggered 事件本身异步发布到事件总线） |


### 设备控制上下文发布


| 事件                    | 携带数据                              | 触发条件     |
| --------------------- | --------------------------------- | -------- |
| SensorDataUpdated     | sensor_id, value, unit, timestamp | 传感器数据更新  |
| ActuatorStateChanged  | actuator_id, state                | 执行机构状态变化 |
| GantryPositionUpdated | position_mm, direction            | 龙门位置更新   |
| DeviceCommFault       | device_id, protocol, error_code   | 通信故障     |


### 指令网关上下文发布

> 发布者：`operational_mode.c`（领域层），由 `event_dispatcher` 拉取后送入事件总线。


| 事件                     | 携带数据           | 触发条件 |
| ---------------------- | ---------------- | ---- |
| OperationalModeChanged | from_mode, to_mode | 模式切换 |


---

## 10. 运行模式状态机

> 当前实现优先规则（2026-04-27）
> `WASHING` 进入由应用层在 `StartWash` 成功后同步提交模式动作完成，
> 不再等待 `WashSessionStarted` 事件。
> 服务完成评估为同步流程，不再依赖
> `PostWashAssessmentCompleted` 事件驱动模式收敛。
> `Recover` 由 `command_handler` 同步调用 `recovery_service_execute()`，
> 不再通过 `RecoveryRequested` / `RecoveryCompleted` 事件编排。

```
              StartWashCommand（BR-01）
  ┌─────────────────────────────────────────────────────┐
  │                                                     ▼
  │  StartSelfCheckCmd  ┌────────────┐  WashCompleted(PostWash:无blocking)
  │  ────────────────→  │ SELF_CHECK │ ───────────────────────────────────┐
  │                     └─────┬──────┘                                    │
  │  EnterManualCmd           │ SelfCheckCompleted                        │
  │  ────────────────→        ▼                                           │
  │              ┌──────────────────┐◄──────────────────────────────────┐ │
  │              │       IDLE       │                                   │ │
  │              └────────┬─────────┘                                   │ │
  │                       │                                             │ │
  │                  EnterManualCmd                                     │ │
  │                       ▼                                             │ │
  │              ┌─────────────────┐                                    │ │
  │              │     MANUAL      │                                    │ │
  │              │ 允许：           │                                    │ │
  │              │  ManualActuator │                                    │ │
  │              │  RecoverCmd†    │                                    │ │
  │              └────────┬────────┘                                    │ │
  │                       │ RecoverCmd†                                 │ │
  │    ┌──────────────────────────────────────────────────────┐        │ │
  │    │  进入 EXCEPTION 的三个来源：                         │        │ │
  │    │  A. WASHING 中 CRITICAL 报警 / WashAborted(CRITICAL) │        │ │
  │    │  B. EStop 触发（任意模式）                           │        │ │
  │    │  C. 洗车结束后的同步评估判定 enter_exception=true      │        │ │
  │    └──────────────────┬───────────────────────────────────┘        │ │
  │                       ▼                                            │ │
  │              ┌──────────────────┐                                  │ │
  │              │    EXCEPTION     │                                  │ │
  │              │ 允许：           │                                  │ │
  │              │  ManualActuator │                                  │ │
  │              │  RecoverCmd†    │                                  │ │
  │              └────────┬─────────┘                                  │ │
  │                       │ RecoverCmd†                                │ │
  │                       │ （清除所有报警）                            │ │
  └───────────────────────┼────────────────────────────────────────────┘ │
                          ▼                                               │
               ┌─────────────────────┐                                   │
               │     RECOVERING      │                                   │
               │ 并行驱动其余机构    │                                   │
               │ 到安全位置          │                                   │
               └──────┬──────┬───────┘                                   │
                      │      │                                           │
               全部到位   超时有未到位                                    │
                      │      │                                           │
                      ▼      ▼                                           │
                    IDLE  EXCEPTION                                  │
                      │                                                  │
                      └──────────────────────────────────────────────────┘

† RecoverCmd 前提：EStop 报警未激活（BR-08）
```

### 指令权限速查表

```
指令                   IDLE  WASHING  MANUAL  SELF_CHECK  EXCEPTION  RECOVERING
─────────────────────────────────────────────────────────────────────────────────
StartWashCommand        ✓     ✗        ✗       ✗           ✗              ✗
StartSelfCheckCommand   ✓     ✗        ✗       ✗           ✗              ✗
EnterManualCommand      ✓     ✗        ✗       ✗           ✗              ✗
ManualActuatorCommand   ✗     ✗        ✓       ✗           ✓              ✗
AbortWashCommand        ✗     ✓        ✗       ✗           ✗              ✗
RecoverCommand          ✗     ✗        ✓†      ✗           ✓†             ✗
─────────────────────────────────────────────────────────────────────────────────
† EStop 报警未激活时方可执行
```

---

## 11. 报警体系

### 11.1 报警等级与处置策略


| 等级       | 处置策略                                                                | 清除方式                    | 对服务的影响                                                      |
| -------- | ------------------------------------------------------------------- | ----------------------- | ----------------------------------------------------------- |
| CRITICAL | 立即停止所有执行机构，进入 EXCEPTION                                             | ON_MOTION 或 AUTO_STATIC | 完全阻断；须 Recover 操作解锁                                         |
| MAJOR    | 完成当前洗车会话后执行服务完成评估；若存在 `blocks_next_service=true` 的活跃报警则进入 EXCEPTION | AUTO_STATIC 或 ON_MOTION | `blocks_next_service=true` → EXCEPTION；`false` → 携带报警允许继续服务 |
| MINOR    | 记录并上报，不干预运行                                                         | AUTO_STATIC             | 无影响                                                         |


### 11.2 MAJOR 报警完整生命周期

```
[检测到异常条件]
        ↓
  AlarmTriggered（MAJOR）立即上报云端 + 写入日志
        ↓
  ┌─────────────────────────────────┐
  │  若正在洗车（WASHING）：         │
  │  · 记录到会话活跃报警列表        │
  │  · 会话继续执行（最大服务完成）  │
  │  若非洗车中：直接进入评估       │
  └─────────────────┬───────────────┘
                    ↓
     WashSessionCompleted / WashSessionAborted
                    ↓
         PostWashAssessment
     ┌──────────────┴──────────────┐
     │                             │
blocks_next_service = false   blocks_next_service = true（BR-14）
     │                             │
允许下次启动                   进入 EXCEPTION
（报警仍在注册表活跃）               │
     │                             ▼
     │                    EXCEPTION（报警仍活跃）
     │                      可手动操作机构
     │                      EStop 清除后可 Recover
     │                             │
     │                       RecoverCommand
     │                             │
     │                    清除所有活跃报警 → RECOVERING → IDLE
     │                             │
     └─────────────────────────────┘
               报警清除路径（两种）
     AUTO_STATIC    → 条件自动恢复 → AlarmCleared（自动）
     ON_MOTION   → 机构被驱动（任何模式）→ 系统重新评估 → 条件消除 → AlarmCleared（自动）
                   或：Recover 操作 → AlarmCleared（批量）
```

### 11.3 报警码分配表（初稿）


| 报警码      | 等级       | 描述           | 清除方式        | 阻断下次服务 |
| -------- | -------- | ------------ | ----------- | ------ |
| 0x020001 | MAJOR    | 主水泵水压过低      | AUTO_STATIC | true   |
| 0x020002 | MAJOR    | 侧刷电机过流       | ON_MOTION   | true   |
| 0x020003 | CRITICAL | 侧刷电机堵转       | ON_MOTION   | —      |
| 0x020004 | MAJOR    | 顶刷电机过流       | ON_MOTION   | true   |
| 0x020005 | CRITICAL | 顶刷电机堵转       | ON_MOTION   | —      |
| 0x020006 | MAJOR    | 龙门位置传感器异常    | ON_MOTION   | true   |
| 0x030001 | CRITICAL | CAN 总线通信中断   | AUTO_STATIC | —      |
| 0x030002 | MAJOR    | Modbus 设备无响应 | AUTO_STATIC | true   |
| 0x040001 | CRITICAL | 急停按钮触发       | AUTO_STATIC | —      |
| 0x040002 | MAJOR    | 防护区域检测异常     | AUTO_STATIC | true   |
| 0x010001 | MAJOR    | 车辆画像识别失败     | AUTO_STATIC | true   |
| 0x010002 | MAJOR    | 车辆超长         | AUTO_STATIC | true   |
| 0x010003 | MAJOR    | 车辆超高         | AUTO_STATIC | true   |
| 0x010004 | MAJOR    | 车辆轮廓数据不可信    | AUTO_STATIC | true   |
| 0x010005 | CRITICAL | 车辆在洗车过程中移动   | AUTO_STATIC | —      |
| 0x050001 | MINOR    | 云端连接断开       | AUTO_STATIC | false  |


> 注：此表为初稿，实际报警码需根据硬件设计和现场调试完善。
> OTA 升级包可用属于通知型消息，不具备报警语义，不纳入报警体系，由云端接入上下文单独处理。

### 11.4 AlarmInstance 状态机（简化模型）

```
AlarmInstance（报警实例）:
  alarm_code    : 对应 AlarmDefinition.alarm_code
  triggered_at  : 触发时间
  state         : ACTIVE | CLEARED

状态转换：

  ACTIVE ──────────────────────────────────────────→ CLEARED

  触发清除的三条路径：
    ① AUTO_STATIC 条件自动恢复
       AlarmRegistry 持续检测，传感器值恢复正常 → 自动清除
    ② ON_MOTION 运动触发重评估
       相关机构被驱动（任何模式）
       → 系统重新评估报警条件 → 条件消除 → 自动清除
    ③ Recover 操作批量清除
       alarm_registry_recover_all() → 清除所有 ACTIVE 实例（EStop 除外）

约束：
  · 无 ACKNOWLEDGED / CLEARING 中间状态
  · 同一 alarm_code 同一时刻只允许一个 ACTIVE 实例（重复触发被忽略）
  · CLEARED 实例归档至历史日志后从 active_alarms[] 移除，不保留在内存
```

---

## 12. WashSession 聚合详细设计

### 12.1 聚合内部结构

WashSession 是聚合根，PassExecutor 值内嵌其中，职责严格分离：

```
WashSession（聚合根）
│  职责：会话级别的生命周期管理
│  · 持有程序快照，知道"总共几趟"
│  · 持有 VehicleProfile，知道“当前这辆车识别到了什么程度”
│  · 持有 RuntimeWashPlan，知道“这一趟这辆车该怎么洗”
│  · 记录"当前第几趟"，推进趟序列
│  · 收到 MAJOR 报警时记录，收到 CRITICAL 报警时中止
│  · 所有会话级领域事件从这里发出
│
├── VehicleProfile（会话内值对象）
│    职责：表示当前车辆的前/后边界、长度、轮廓采样、置信度
│
├── ProfileAcquisition（会话内领域服务/组件）
│    职责：消费龙门传感器数据，逐步识别 front/rear/roof profile
│
├── RuntimeWashPlan（会话内值对象）
│    职责：把 WashProgram 模板解析成“本车可执行”的动作窗口与降级策略
│
└── PassExecutor（值内嵌，非指针，每趟复用同一实例）
       职责：单趟的执行细节
       · 执行运动序列（MOVE / STOP）
       · MOVE 阶段：按龙门位置 + 车辆画像评估 GANTRY/VEHICLE 触发器
       · STOP 阶段：按停止后计时评估 TIME 触发器
       · 趟完成后回调通知 WashSession
```

---

### 12.2 WashSession 状态机

```
状态列表：
  CREATED     → 会话已创建，等待启动
  RUNNING     → 趟序列执行中（PassExecutor 持续工作）
  COMPLETING  → 最后一趟完成，执行专用收尾逻辑（龙门归位、风机延时停）
  COMPLETED   → 会话正常完成
  ABORTING    → 收到中止信号，等待 PassExecutor 停止所有机构
  ABORTED     → 会话已中止

转换规则：
  CREATED    + StartWashCommand              → RUNNING（PassExecutor 启动趟0）
  RUNNING    + 当前趟完成（非最后趟）         → RUNNING（PassExecutor 重置，启动下一趟）
  RUNNING    + 当前趟完成（最后一趟）         → COMPLETING
  RUNNING    + AlarmTriggered(CRITICAL)     → ABORTING（abort_cause=CRITICAL_ALARM）
  RUNNING    + AbortCommand                 → ABORTING（abort_cause=MANUAL）
  RUNNING    + PassAborted(STEP_TIMEOUT)    → ABORTING（abort_cause=STEP_TIMEOUT）
  COMPLETING + 收尾完成                     → COMPLETED
  COMPLETING + AlarmTriggered(CRITICAL)     → ABORTING（abort_cause=CRITICAL_ALARM）
  ABORTING   + 机构停止确认                 → ABORTED

COMPLETED 和 ABORTED 均发布领域事件，由应用层订阅并分发（模式转换 + 服务完成评估）。

指令网关根据 WashSessionAborted.abort_cause 决定后续模式：
  CRITICAL_ALARM → EXCEPTION
  MANUAL         → IDLE
  STEP_TIMEOUT   → EXCEPTION
```

**COMPLETING 说明**：最后一趟完成后的收尾动作（龙门归位、风机延时停止等）不属于任何趟，
由 WashSession 直接驱动，完成后转为 COMPLETED。

---

### 12.3 PassExecutor 状态机

```
状态列表：
  IDLE     → 未执行，等待配置和启动
  MOVING   → 正在执行 MOVE 步骤，龙门运动中
  STOPPED  → 正在执行 STOP 步骤，龙门静止计时
  ABORTING → 收到中止，正在关停所有机构
  COMPLETED → 运动序列全部执行完毕，趟正常完成
  ABORTED  → 趟已中止

转换规则：
  IDLE     + start(pass)              → MOVING（执行第一个 MOVE）
  MOVING   + 到达 target_position     → STOPPED（若下一步是 STOP）
                                      → MOVING （若下一步还是 MOVE）
                                      → COMPLETED（若无更多步骤）
  MOVING   + 通信超时（无位置更新）   → ABORTING（触发 0x030xxx 通信报警）
  MOVING   + 行程超时（应到未到）     → ABORTING（触发 0x020xxx 设备报警）
  STOPPED  + stop_elapsed >= duration → MOVING（若下一步是 MOVE）
                                      → COMPLETED（若无更多步骤）
  任意状态 + abort()                  → ABORTING
  ABORTING + 所有机构停止确认         → ABORTED

注：通信超时与行程超时对应不同报警码，维修指导不同：
  通信超时（0x03xxxx）→ 检查传感器线路和通信总线
  行程超时（0x02xxxx）→ 检查电机、传动机构和限位开关
```

---

### 12.4 PassExecutor 核心执行逻辑（运动序列 + 车辆画像驱动触发）

```
【维护的状态量】
  current_step_idx  : 当前执行的运动步骤序号
  gantry_pos_mm     : 龙门当前位置（MOVE 阶段持续更新）
  stop_elapsed_ms   : 当前 STOP 已持续时间（STOP 阶段累加，MOVE 阶段无意义）
  last_pos_update   : 上次收到位置更新的时间戳（用于检测通信超时）
  vehicle_profile   : 当前车辆画像（由 ProfileAcquisition 持续更新）
  runtime_plan      : 当前会话已解析出的动作窗口（随画像状态增量更新）

【执行 MOVE 步骤时】
  请求龙门以 speed 向 target_position 移动

  每次收到 GantryPositionUpdated（事件驱动）：
    更新 gantry_pos_mm 和 last_pos_update
    将龙门上传的距离/接触/高度传感器样本喂给 ProfileAcquisition：
      UNKNOWN        → ACQUIRING
      识别到车头     → PARTIAL_LOCKED（front_edge_mm 已知）
      识别到车尾     → FULL_LOCKED（rear_edge_mm / length_mm 已知）
      数据异常       → FAILED

    若 VehicleProfile 版本发生变化：
      RuntimeWashPlan ← 由 WashProgram 模板 + VehicleProfile 重新解析

    评估当前动作激活策略：
      1. GANTRY_POSITION：按龙门绝对位置判断
      2. VEHICLE_POSITION：按 FRONT / REAR / CENTER 锚点解析为绝对位置后判断
      3. VEHICLE_RATIO：按整车长度比例解析为绝对窗口后判断
      4. PROFILE_STATE：判断车辆画像是否达到所需锁定状态
      5. 安全条件（报警、压力、电流、接触反馈）不满足时强制关闭
    仅当 ActivationPolicy 满足且未触发 fallback 禁止条件时，激活机构/药剂
    gantry_pos_mm 到达 target_position → 切换到下一运动步骤

  每次 tick（主循环周期性调用，与事件无关）：
    检测通信超时：now - last_pos_update > COMM_TIMEOUT → 触发通信报警（0x03xxxx）
    检测行程超时：move_elapsed_ms > TRAVEL_TIMEOUT → 触发行程报警（0x02xxxx）
    ← 注：超时检测必须在 tick 中主动检查，不能放在 GantryPositionUpdated 处理里；
         收不到事件时才是需要报超时的场景，事件处理里永远不会执行到

【执行 STOP 步骤时】
  请求龙门停止，stop_elapsed_ms 从 0 开始累加
  每次 tick：
    评估所有 TIME 触发器：
      stop_elapsed_ms ∈ [start_ms, stop_ms] → 激活机构/药剂
      否则 → 关闭
    stop_elapsed_ms >= duration → 切换到下一运动步骤

【运动序列全部执行完成时】
  关闭所有本趟仍激活的机构和药剂
  state → COMPLETED，回调通知 WashSession

【推荐执行策略】
  Pass 0（PROFILE_ACQUIRE_PASS）：
    · 边移动边识别车辆前后边界与轮廓
    · 仅允许预冲水/泡沫等低风险动作

  Pass 1..N：
    · 当 VehicleProfile = FULL_LOCKED 时，优先使用 VEHICLE_POSITION / VEHICLE_RATIO 精准触发
    · 若识别失败，则按 FallbackPolicy 退回通用窗口或跳过高风险机构
```

---

### 12.5 报警与会话的交互规则


| 报警等级       | WashSession 响应                                              | PassExecutor 响应         |
| ---------- | ----------------------------------------------------------- | ----------------------- |
| CRITICAL   | state → ABORTING，abort_cause 记录来源，调用 executor.abort()       | state → ABORTING，关停所有机构 |
| MAJOR      | 追加 alarm_code 到 major_alarm_journal[]，继续执行（append-only，不删除） | 不受影响，继续执行               |
| MAJOR（已清除） | 不处理（journal 只追加，清除状态由 AlarmRegistry 权威维护）                   | 不受影响                    |
| MINOR      | 不记录，不影响状态                                                   | 不受影响                    |


**报警上报与服务评估是两条独立流水线**，WashSession 不负责上报：

```
流水线①（立即上报，与会话无关）：
  SafetyMonitor 检测到异常
    → 发布 AlarmTriggered 事件
    → CloudService 订阅 → 立即上报云端物模型
    → MaintenanceLog 订阅 → 写入本地日志

流水线②（服务评估，会话结束后）：
  应用层订阅 AlarmTriggered(MAJOR) → 调用 wash_session_on_alarm_triggered()
                                    → 追加 alarm_code 到 major_alarm_journal[]
  会话结束 → 发布 WashSessionCompleted/Aborted（携带 session_major_alarms[]）
           → 应用层订阅会话结束事件 → 调用 alarm_registry_assess_after_session()
             → 执行同步服务评估
               （以 session_major_alarms[] 为输入，
                 交叉查询自身 active_alarms[] 确定"仍活跃 + blocks_next_service"的报警）
```

---

### 12.6 WashSession 对外操作接口

```
/* 指令（来自指令网关）*/
wash_session_start(session, program)        → 深拷贝程序为快照，启动趟0
wash_session_abort(session, abort_cause)    → 人工中止
wash_session_tick(session)                  → 主循环每 tick 调用，推进 ProfileAcquisition、RuntimeWashPlan 和 PassExecutor

/* 事件/调用输入（应用层桥接 + 主循环直接调用）*/
wash_session_on_alarm_triggered(session, code, level)
  ← MAJOR 级时追加到 major_alarm_journal[]（去重：同一 code 只记录一次；不需要 on_alarm_cleared）
wash_session_on_sensor_sample(session, sample)
  ← 喂给 ProfileAcquisition；必要时更新 VehicleProfile / RuntimeWashPlan
wash_session_on_pass_completed(session)     ← PassExecutor 回调
wash_session_on_pass_aborted(session, cause)← PassExecutor 回调（含超时原因）
wash_session_on_completing_done(session)    ← 收尾完成回调

/* 查询 */
wash_session_get_state(session)             → 当前状态
wash_session_get_pass_idx(session)          → 当前趟序号
wash_session_has_alarm_in_journal(session, code) → 本会话是否出现过指定报警
wash_session_get_vehicle_profile(session)   → 当前车辆画像
wash_session_is_profile_ready(session, required_status) → 画像是否达到要求锁定状态

/* 领域事件取出（由应用层负责分发到事件总线）*/
wash_session_pull_events(session, buf, max) → 最多取出 max 条并从队列移除；未取出的事件保留，下次调用继续
```

---

### 12.7 WashSession 发布的领域事件


| 事件                   | 触发时机                   | 携带数据                                            |
| -------------------- | ---------------------- | ----------------------------------------------- |
| WashSessionCompleted | COMPLETING → COMPLETED | session_id, session_major_alarms[]              |
| WashSessionAborted   | ABORTING → ABORTED     | session_id, abort_cause, session_major_alarms[] |


---

## 13. OperationalMode 聚合详细设计

### 13.1 聚合职责

OperationalMode 是指令网关上下文的聚合根，负责：

- 持有唯一的当前运行模式状态
- 对所有外部指令做合法性校验（当前模式 + 急停条件）
- 执行由**本聚合不变量决定**的模式转换
- 发布模式变更领域事件

**所有外部指令必须经过 OperationalMode 仲裁，不允许绕过。**

> **重要边界**：OperationalMode 聚合不订阅事件总线，也不直接调用其他聚合。
> 由应用层（`command_handler.c`）订阅事件总线，再调用 `op_mode_on_xxx()` 将事件喂入聚合，
> 保持领域层对基础设施（事件总线）零依赖。
> Recover 的跨聚合协调（清除报警 + 驱动机构归位）由 `recovery_service.c` 应用服务负责。

---

### 13.2 聚合内部结构

```
OperationalMode（聚合根）:
  current_mode    : OperationalMode 枚举值（当前运行模式）
  estop_active    : bool（急停报警当前是否激活，影响 RecoverCommand 可用性）
  domain_events[] : 待发布的领域事件队列

注：
  · blocking_warning_exists 由 AlarmRegistry 持有；
    OperationalMode 不直接持有报警列表，必要时通过接口查询。
  · estop_active 由应用层接收 EStopTriggered / AlarmCleared(EStop) 事件后，
    调用 op_mode_on_estop_triggered() / op_mode_on_estop_cleared() 更新；
    聚合本身不订阅事件总线。
```

---

### 13.3 指令合法性校验（表驱动）

```
指令合法性由"当前模式"和"附加条件"共同决定：

                    IDLE  WASHING  MANUAL  SELF_CHECK  EXCEPTION  RECOVERING
─────────────────────────────────────────────────────────────────────────────────
StartWashCommand     ✓     ✗        ✗       ✗           ✗              ✗
StartSelfCheckCmd    ✓     ✗        ✗       ✗           ✗              ✗
EnterManualCommand   ✓     ✗        ✗       ✗           ✗              ✗
ManualActuatorCmd    ✗     ✗        ✓       ✗           ✓              ✗
AbortWashCommand     ✗     ✓        ✗       ✗           ✗              ✗
RecoverCommand       ✗     ✗        ✓†      ✗           ✓†             ✗
─────────────────────────────────────────────────────────────────────────────────
† 附加条件：estop_active == false（BR-08）

校验流程：
  1. 查表：当前模式下该指令是否允许（ALLOWED / DENIED / CONDITIONAL）
  2. CONDITIONAL 指令：进一步检查附加条件（如 estop_active）
  3. 返回 CommandResult { ALLOWED } 或 CommandResult { DENIED, reason }
  4. DENIED 时直接返回拒绝结果，不发布额外领域事件
```

---

### 13.4 模式转换来源

```
模式切换由两类来源触发：

【指令触发】（外部指令，经过合法性校验后执行）
  EnterManualCommand（在 IDLE）   → IDLE → MANUAL
  StartWashCommand（在 IDLE）     → 转交洗车执行，并同步提交 WASH_STARTED 模式动作
  StartSelfCheckCommand（在 IDLE）→ IDLE → SELF_CHECK
  RecoverCommand（在 MANUAL/EXCEPTION，EStop 未激活）
                                  → MANUAL/EXCEPTION → RECOVERING
                                  → 应用层同步调用 recovery_service_execute()

【事件触发】（订阅其他上下文的领域事件）
  WashSessionCompleted            → WASHING → 会话结束后同步评估，再收敛到 IDLE / EXCEPTION
  WashSessionAborted(CRITICAL_ALARM / STEP_TIMEOUT)
                                  → WASHING → EXCEPTION
  WashSessionAborted(MANUAL)      → WASHING → IDLE
  SelfCheckCompleted              → SELF_CHECK → IDLE
  AlarmTriggered(CRITICAL)        → IDLE / MANUAL / SELF_CHECK → 直接进入 EXCEPTION
                                  → WASHING    → 忽略（由 WashSession 先行处理，
                                                   最终经 WashSessionAborted 收敛）
                                  → EXCEPTION  → 已在异常态，幂等保持
                                  → RECOVERING → 直接进入 EXCEPTION
  EStopTriggered                  → 任意 → EXCEPTION
                                  （若已在 EXCEPTION/RECOVERING 则保持，更新 estop_active）
  AlarmCleared(EStop)             → 更新 estop_active = false（不改变模式）
```

---

### 13.5 OperationalMode 对外操作接口

```
/* 指令处理（所有外部指令入口，返回允许/拒绝）*/
op_mode_handle_command(mode, cmd_type, cmd_payload)
  → CommandResult { ALLOWED } | CommandResult { DENIED, reason }

/* 事件输入（来自事件总线订阅）*/
op_mode_on_wash_session_completed(mode)
op_mode_on_wash_session_aborted(mode, abort_cause)
op_mode_on_self_check_completed(mode)
op_mode_on_alarm_triggered(mode, alarm_code, level)
  ← ALARM_ESTOP：跳过（safety_thread 已通过快速通道更新 OperationalMode，保证幂等）
  ← CRITICAL 级（非 ESTOP）：非 WASHING 模式时立即转入 EXCEPTION
  ← WASHING 模式下忽略（由 WashSession 先行处理，最终通过 WashSessionAborted 到达）
op_mode_on_estop_triggered(mode)
op_mode_on_estop_cleared(mode)

/* 查询 */
op_mode_get_current(mode)       → OperationalMode 枚举值
op_mode_is_estop_active(mode)   → bool

/* 领域事件取出（由应用层负责分发到事件总线）*/
op_mode_pull_events(mode, buf, max) → 最多取出 max 条并从队列移除；未取出的事件保留，下次调用继续
```

### 13.6 OperationalMode 发布的领域事件


| 事件                     | 触发时机     | 携带数据           |
| ---------------------- | -------- | ---------------- |
| OperationalModeChanged | 任何模式转换   | from_mode, to_mode |

---

## 14. AlarmRegistry 聚合详细设计

### 14.1 聚合职责

AlarmRegistry 是安全管理上下文的聚合根，负责：

- 持有所有报警定义（AlarmDefinition 静态配置表）
- 管理当前所有活跃报警实例（AlarmInstance）
- **A类（AUTO_STATIC）**：由应用层每 tick 喂入传感器数据（`alarm_registry_on_sensor_updated()`），自动检测和自动清除
- **B类（ON_MOTION）**：被动接收外部触发；机构被驱动时自动重新评估，条件消除则自动清除，或由 Recover 批量清除
- 执行服务完成评估（PostWashAssessment）
- 响应 Recover 操作批量清除所有报警
- 发布 AlarmTriggered / AlarmCleared 事件

---

### 14.2 聚合内部结构

```
AlarmRegistry（聚合根）:
  definitions[]   : AlarmDefinition 数组（静态，系统启动加载，只读）
  active_alarms[] : AlarmInstance 数组（当前所有 ACTIVE 实例，上限 MAX_ACTIVE_ALARMS）
  domain_events[] : 待发布的领域事件队列

AlarmInstance:
  alarm_code    : 对应 AlarmDefinition.alarm_code
  triggered_at  : 触发时间戳
  state         : ACTIVE | CLEARED

约束：
  · CLEARED 实例归档日志后立即从 active_alarms[] 移除，不保留在内存
  · 同一 alarm_code 同一时刻只允许一个 ACTIVE 实例（重复触发忽略）
```

---

### 14.3 报警检测模型（混合检测）

```
A类（AUTO_STATIC，定义驱动自检）：
  · AlarmDefinition.has_auto_detect = true
  · 应用层在每次主循环 tick 采集传感器数据后，直接调用
    alarm_registry_on_sensor_updated()，而非通过事件总线订阅。
    （传感器原始值是基础设施输入，不具备领域事件语义；
      若经事件总线分发，则将硬件采样误用为领域行为，
      且掩盖了"采样→检测"之间的直接因果关系，使代码更难推理）
  · 每次传感器更新时，遍历所有 has_auto_detect=true 的定义，评估检测条件：
      条件触发 + 无活跃实例 → 创建 AlarmInstance，发布 AlarmTriggered
      条件恢复 + 有活跃实例 → 清除实例，发布 AlarmCleared
  · EStop（0x040001）属于此类：sensor_poll 每 tick 检测 GPIO 状态，
      信号激活 → alarm_registry_on_sensor_updated() → AlarmInstance 创建，发布 AlarmTriggered(ALARM_ESTOP)
      信号释放 → alarm_registry_on_sensor_updated() → AlarmInstance 清除，发布 AlarmCleared(ALARM_ESTOP)
    注：AlarmRegistry 只负责 ALARM_ESTOP 的实例记录与事件发布；
        OperationalMode 的 EStop 状态由 safety_thread 快速通道直接更新（见 §15.6），
        两者职责分离；存在 ≤20ms 窗口期不一致，这是为保证急停快速响应接受的显式 trade-off（见 §15.6）。

B类（ON_MOTION，外部触发）：
  · AlarmDefinition.has_auto_detect = false
  · 由外部组件主动调用 alarm_registry_trigger()：
      PassExecutor    → 通信超时（0x030xxx）、行程超时（0x020xxx）
      DeviceControl   → 硬件通信故障
      业务逻辑        → 其他运行异常
  · 清除方式：
      相关机构被驱动（任何模式均可触发），系统自动重新评估报警条件：
        条件消除 → alarm_registry_clear()（自动）
      或由 Recover 操作调用 alarm_registry_recover_all() 批量清除
```

---

### 14.4 AlarmInstance 状态机

```
ACTIVE ────────────────────────────────────────────→ CLEARED

三条清除路径：
  ① 自动清除（A类，AUTO_STATIC）
     检测条件恢复 → AlarmRegistry 内部清除 → 发布 AlarmCleared

  ② 运动触发重评估（B类，ON_MOTION）
     相关机构被驱动（任何模式）
     → 系统重新评估报警条件，条件消除 → alarm_registry_clear(alarm_code)
     → 发布 AlarmCleared

  ③ Recover 批量清除
     alarm_registry_recover_all()
     → 清除所有 ACTIVE 实例（EStop 报警除外：其状态由硬件信号决定）
     → 逐个发布 AlarmCleared

无 ACKNOWLEDGED / CLEARING 中间状态。
```

---

### 14.5 服务完成评估（PostWashAssessment）

> 当前实现优先规则（2026-04-27）
> 当前代码中已取消 `PostWashAssessmentCompleted` 事件。会话结束后，
> 应用层直接调用 `alarm_registry_assess_after_session()`，再同步提交
> `WASH_COMPLETED_NO_BLOCKING`、`WASH_COMPLETED_BLOCKING` 或
> `WASH_ABORTED` 模式动作。以下若出现基于该事件的流程，均视为历史草案。

```
触发：应用层收到 WashSessionCompleted 或 WashSessionAborted 后，
      直接调用 alarm_registry_assess_after_session()

评估逻辑（交叉查询，AlarmRegistry 为权威事实源）：
  输入：session_event.session_major_alarms[]（本次会话期间出现过的 MAJOR 报警码 journal）

  for each alarm_code in session_major_alarms[]:
      if alarm_registry_is_active(registry, alarm_code):       ← 当前仍活跃？
          def = lookup_definition(alarm_code)
          if def.blocks_next_service:
              → 加入 blocking_alarms[]

  若 blocking_alarms[] 非空 → enter_exception = true
  否则                      → enter_exception = false

  返回 assessment {
    blocking_alarms[],
    enter_exception
  }

下游响应：
  应用层同步提交模式动作：
    enter_exception = true  → 转入 EXCEPTION
    enter_exception = false → 留在 IDLE
```

---

### 14.6 AlarmRegistry 对外操作接口

> 当前实现优先规则（2026-04-27）
> 以下接口名若与代码不一致，以 `src/domain/safety/alarm_registry.h`
> 为准。当前实现有效接口包括：
> `alarm_registry_assess_after_session()`、
> `alarm_registry_get_active_alarms()`、
> `alarm_registry_has_active_critical()`、
> `alarm_registry_has_active_blocking_service()`、
> `alarm_registry_recover_all()`。
> `alarm_registry_on_wash_session_ended()`、
> `alarm_registry_get_active_warnings()`、
> `alarm_registry_is_estop_active()` 已废弃，不再作为编码依据。

```
/* 传感器输入 */
alarm_registry_on_sensor_updated(registry, sensor_id, value)
  ← A类检测：触发/清除 AUTO_STATIC 报警

/* 服务完成评估 */
alarm_registry_assess_after_session(registry, session_major_alarms, count, assessment)
  ← 基于仍然活跃且 blocks_next_service 的 MAJOR 报警做同步评估

/* B类报警外部触发/清除 */
alarm_registry_trigger(registry, alarm_code)  → AlarmResult
  ← PassExecutor / DeviceControl / 业务逻辑调用
  ← 若已有 ACTIVE 实例则忽略（返回 ALREADY_ACTIVE）

alarm_registry_clear(registry, alarm_code)    → AlarmResult
  ← 手动操作验证通过后调用
  ← 若无 ACTIVE 实例则忽略（返回 NOT_ACTIVE）

/* Recover 操作 */
alarm_registry_recover_all(registry)
  ← 清除所有 ACTIVE 实例（EStop 报警由硬件信号控制，Recover 不强制清除）
  ← 每个清除的实例发布 AlarmCleared 事件

/* 查询 */
alarm_registry_is_active(registry, alarm_code) → bool
alarm_registry_get_active_alarms(registry, buf, max) → count
alarm_registry_has_active_critical(registry)         → bool
alarm_registry_has_active_blocking_service(registry) → bool

/* 领域事件取出（由应用层负责分发到事件总线）*/
alarm_registry_pull_events(registry, buf, max)  → 最多取出 max 条并从队列移除；未取出的事件保留，下次调用继续
```

### 14.7 AlarmRegistry 发布的领域事件


| 事件             | 触发时机            | 携带数据                                 |
| -------------- | --------------- | ------------------------------------ |
| AlarmTriggered | 新报警实例创建（ACTIVE） | alarm_code, level, triggered_at      |
| AlarmCleared   | 报警实例清除          | alarm_code, clear_method, cleared_at |


---

## 15. 代码架构设计

### 15.1 分层架构

DDD 四层架构映射到嵌入式 C 项目：

```
┌──────────────────────────────────────────────────────┐
│  main.c  系统入口：初始化 + 线程启动                  │
└───────────────────────┬──────────────────────────────┘
                        ↓
┌──────────────────────────────────────────────────────┐
│  application/  应用层                                 │
│  · 接收外部指令，经 OperationalMode 仲裁后调用聚合     │
│  · 每次主循环 tick 从聚合拉取领域事件，发布到事件总线   │
│  · ProfileBridge / Recovery / SelfCheck 应用服务      │
└──────────┬───────────────────────────┬───────────────┘
           ↓                           ↓
┌──────────────────────┐   ┌───────────────────────────┐
│  domain/  领域层      │   │  infrastructure/  基设层   │
│  · WashSession        │   │  · DeviceControl 适配器    │
│  · VehicleProfile /   │   │    (CAN/Modbus/GPIO)       │
│    ProfileAcquisition │   │  · Cloud ACL 防腐层        │
│  · RuntimeWashPlan    │   │  · 本地存储（程序配置）     │
│  · OperationalMode    │   │                           │
│  · AlarmRegistry      │   │                           │
│  · 纯业务逻辑         │   │                           │
│  · 零外部依赖         │   │                           │
└──────────────────────┘   └──────────────┬────────────┘
                                          ↓
                           ┌───────────────────────────┐
                           │  platform/  平台层         │
                           │  · 事件总线（队列+订阅表）  │
                           │  · Safety Thread（EStop）  │
                           │  · Main Loop Thread        │
                           │  · 互斥锁 / 定时器封装      │
                           └───────────────────────────┘
```

**核心约束**：`domain/` 的源文件只能 `#include` 标准库和 `domain/shared/`，
禁止 include `infrastructure/` / `platform/` / `application/`，编译时强制检查。

---

### 15.2 目录结构

```
carwash/
├── src/
│   ├── domain/
│   │   ├── shared/
│   │   │   ├── domain_types.h       ← 基础类型（AlarmCode, SessionId 等）
│   │   │   └── domain_events.h      ← 所有领域事件结构体定义
│   │   ├── wash_execution/          ← 洗车执行上下文（核心域）
│   │   │   ├── ports.h              ← 接口定义（由领域层定义，基设层实现）
│   │   │   ├── wash_program.h/.c    ← WashProgram / WashPass 值对象
│   │   │   ├── trigger_condition.h/.c ← GANTRY/VEHICLE/TIME 触发条件与激活策略
│   │   │   ├── vehicle_profile.h/.c ← VehicleProfile 值对象
│   │   │   ├── profile_acquisition.h/.c ← 车辆轮廓在线采集与锁定逻辑
│   │   │   ├── runtime_wash_plan.h/.c ← 基于本车画像解析的运行时计划
│   │   │   ├── pass_executor.h/.c   ← PassExecutor（内嵌到 WashSession）
│   │   │   └── wash_session.h/.c    ← WashSession 聚合根
│   │   ├── safety/                  ← 安全管理上下文（支撑域）
│   │   │   ├── alarm_definition.h   ← AlarmDefinition 静态表
│   │   │   └── alarm_registry.h/.c  ← AlarmRegistry 聚合根
│   │   └── command_gateway/         ← 指令网关上下文（支撑域）
│   │       └── operational_mode.h/.c← OperationalMode 聚合根
│   │
│   ├── application/
│   │   ├── app_init.h/.c            ← 启动：注册订阅、注入依赖、静态实例初始化
│   │   ├── command_handler.h/.c     ← 外部指令入口 → 仲裁 → 调用聚合
│   │   ├── event_dispatcher.h/.c    ← 每 tick 从聚合拉取事件 → 发布到事件总线
│   │   ├── profile_bridge.h/.c      ← 传感器样本 → wash_session_on_sensor_sample() 桥接
│   │   ├── recovery_service.h/.c    ← Recovery 应用服务（跨聚合协调：清除报警 + 驱动归位）
│   │   └── self_check_service.h/.c  ← SelfCheck 应用服务（协调多个聚合）
│   │
│   ├── infrastructure/
│   │   ├── device/IGantryPort
│   │   │   ├── gantry_actuator_can.h/.c ← wash_gantry_port_t 的 CAN 实现
│   │   │   ├── sim_gantry_adapter.h/.c  ← demo/test 用模拟龙门适配器
│   │   │   ├── chemical_gpio.h/.c   ← IChemicalPort 的 GPIO 实现
│   │   │   └── sensor_modbus.h/.c   ← ISensorPort 的 Modbus 实现
│   │   ├── cloud/
│   │   │   ├── cloud_port.h         ← 云端统一接口定义
│   │   │   ├── aliyun_adapter.h/.c  ← 阿里云 ACL 实现
│   │   │   ├── aws_adapter.h/.c     ← AWS ACL 实现
│   │   │   └── tuya_adapter.h/.c    ← 涂鸦 ACL 实现
│   │   └── storage/
│   │       └── program_store.h/.c   ← 洗车程序持久化（Flash/文件）
│   │
│   ├── platform/
│   │   ├── event_bus.h/.c           ← 事件队列 + 订阅表
│   │   ├── safety_thread.h/.c       ← EStop 监控线程（SCHED_FIFO）
│   │   └── main_loop.h/.c           ← 主循环线程（20ms tick）
│   │
│   └── main.c
│
├── tests/
│   ├── domain/                      ← 领域层单元测试（无硬件依赖）
│   └── application/                 ← 应用层集成测试
└── CMakeLists.txt
```

---

### 15.3 端口与适配器（Ports & Adapters in C）

> 当前实现优先规则（2026-04-27）
> 本节中的旧装配示例仍保留历史草案描述。当前代码入口以
> `production_bootstrap_run()`、`production_app_init()` 及
> `gantry_actuator_can.*` / `sim_gantry_adapter.*` 为准；旧的
> `app_init()`、`recovery_service_on_requested()`、事件驱动恢复编排
> 均不再作为当前实现依据。

领域层用函数指针结构体定义接口，基设层提供具体实现，运行时注入。

```c
/* === domain/wash_execution/ports.h（领域层定义，不依赖任何硬件）=== */

typedef struct {
    void    (*move_to)(void *ctx, int32_t target_mm, int32_t speed_mm_s);
    void    (*stop)   (void *ctx);
    int32_t (*get_position_mm)(void *ctx);
} IGantryPort;

typedef struct {
    void (*set_state)(void *ctx, ActuatorId id, ActuatorState state);
} IActuatorPort;

typedef struct {
    void (*dispense)(void *ctx, ChemicalId id, int32_t flow_rate_ml_s);
    void (*stop)    (void *ctx, ChemicalId id);
} IChemicalPort;

/* WashSession 创建时注入所有端口 */
typedef struct {
    const IGantryPort   *gantry;
    const IActuatorPort *actuator;
    const IChemicalPort *chemical;
} WashSessionPorts;
```

```c
/* === infrastructure/device/gantry_actuator_can.c（CAN 适配器实现）=== */

static domain_result_t can_gantry_move_to(
    void *ctx,
    int32_t target_mm,
    int32_t speed_mm_s,
    uint32_t timeout_ms
) {
    CanFrame f = build_gantry_move_frame(target_mm, speed_mm_s);
    return can_driver_send_with_timeout(&f, timeout_ms);
}

static domain_result_t can_gantry_stop(void *ctx, uint32_t timeout_ms) {
    CanFrame f = build_gantry_stop_frame();
    return can_driver_send_with_timeout(&f, timeout_ms);
}

static domain_result_t can_gantry_get_position(
    void *ctx,
    int32_t *position_mm,
    uint32_t timeout_ms
) {
    (void)timeout_ms;
    *position_mm = g_gantry_pos_mm;  /* 由 CAN 接收中断持续更新 */
    return DOMAIN_RESULT_OK;
}

static const gantry_actuator_can_driver_t g_gantry_can_driver = {
    .context = NULL,
    .move_to = can_gantry_move_to,
    .stop = can_gantry_stop,
    .get_position = can_gantry_get_position,
};
```

```c
/* === platform/production_bootstrap.c：生产入口 =======================================*/

domain_result_t platform_bootstrap(void) {
    production_bootstrap_config_t config;

    (void)memset(&config, 0, sizeof(config));
    config.gantry_driver = &g_gantry_can_driver;
    config.self_check_config = &g_self_check_config;
    config.drive_safe_state = production_drive_safe_state;
    config.main_loop.max_tick_count = MAIN_LOOP_FOREVER_TICK_COUNT;

    return production_bootstrap_run(&config);
}
```

```c
/* === application/app_init.c：应用装配 ===============================================*/

    /* 注册事件订阅（启动时一次性静态注册）*/
    /* 规则：应用层函数订阅事件，再调用聚合的 on_xxx() 方法；领域聚合不直接订阅事件总线 */

    /* 报警事件 → 通知各聚合 + 上报云端 */
    event_bus_subscribe(EVT_ALARM_TRIGGERED,              cmd_handler_on_alarm_triggered);
      /* cmd_handler_on_alarm_triggered 内部：
           op_mode_on_alarm_triggered(&g_op_mode, evt.alarm_code, evt.level)
                （ALARM_ESTOP → 幂等跳过；CRITICAL + 非WASHING → EXCEPTION）
           wash_session_on_alarm_triggered(&g_wash_session, evt.alarm_code, evt.level)
                （CRITICAL → ABORTING；MAJOR → journal）
           cloud_on_alarm_triggered(evt)                      （立即上报）  */

    /* 洗车会话结束 → 同步评估 + 模式转换 */
    event_bus_subscribe(EVT_WASH_SESSION_COMPLETED,       cmd_handler_on_wash_session_ended);
    event_bus_subscribe(EVT_WASH_SESSION_ABORTED,         cmd_handler_on_wash_session_ended);
      /* cmd_handler_on_wash_session_ended 内部：
           alarm_registry_assess_after_session(...)        （执行同步评估）
           op_mode_on_wash_session_completed/aborted(&g_op_mode, evt)    （模式转换） */

    /* 自检完成 → 模式 SELF_CHECK→IDLE */
    event_bus_subscribe(EVT_SELF_CHECK_COMPLETED,         cmd_handler_on_self_check_completed);
      /* cmd_handler_on_self_check_completed 内部：
           op_mode_on_self_check_completed(&g_op_mode) */

    /* 报警清除 → 云端上报（EStop 清除另有 safety_thread 快速通道，此处不重复驱动模式状态）*/
    event_bus_subscribe(EVT_ALARM_CLEARED,                cmd_handler_on_alarm_cleared);
      /* cmd_handler_on_alarm_cleared 内部：
           cloud_on_alarm_cleared(evt)   （上报云端）
           ※ ALARM_ESTOP 的 estop_active 更新由 safety_thread 直接调用 op_mode_on_estop_cleared()
              完成，此处不再重复调用，避免双路径竞争 */

    /* 模式变更 → 上报云端 */
    event_bus_subscribe(EVT_OP_MODE_CHANGED,              cloud_on_mode_changed);
}
```

---

### 15.4 静态内存分配策略

```c
/* === 聚合实例：进程内单例，BSS 段静态分配 === */
static WashSession     g_wash_session;       /* 洗车机只有一个会话 */
static OperationalMode g_op_mode;            /* 模式唯一 */
static AlarmRegistry   g_alarm_registry;     /* 报警注册表唯一 */

/* === AlarmRegistry 内部池 === */
#define MAX_ACTIVE_ALARMS     32
#define MAX_ALARM_DEFINITIONS 64

/* alarm_registry.c 内部，外部不可见 */
static AlarmInstance   s_instance_pool[MAX_ACTIVE_ALARMS];
static AlarmDefinition s_definition_table[MAX_ALARM_DEFINITIONS];

/* === WashSession 内部池 === */
/* program_snapshot 深拷贝目标：栈上或静态缓冲区 */
static WashProgram s_program_snapshot;       /* wash_session.c 内部 */

/* === 事件总线队列 === */
#define EVT_QUEUE_CAPACITY 64
static DomainEvent   s_evt_buffer[EVT_QUEUE_CAPACITY];  /* platform/event_bus.c */
```

---

### 15.5 事件总线实现

```c
/* === platform/event_bus.h === */

#define MAX_SUBSCRIBERS_PER_EVENT 8

typedef void (*EventHandler)(const DomainEvent *evt);

/* 初始化 */
void event_bus_init(void);

/* 订阅（启动时注册，运行时不变）*/
void event_bus_subscribe(EventType type, EventHandler handler);

/* 发布（线程安全，内部加锁入队）*/
void event_bus_publish(const DomainEvent *evt);

/* 分发（主循环每 tick 调用，逐个出队并调用订阅者）*/
void event_bus_dispatch_all(void);
```

```c
/* === application/event_dispatcher.c：每 tick 从聚合拉取并发布 === */

/*
 * pull_xxx 使用"循环直到清空"模式，避免单次 buf 容量不足时遗漏事件。
 * （单轮可能累计多个聚合事件，超过 buf 大小时需多轮拉取）
 */

static void pull_and_publish(PullFn pull_fn, void *aggregate) {
    DomainEvent buf[8];
    int n;
    do {
        n = pull_fn(aggregate, buf, 8);
        for (int i = 0; i < n; i++) event_bus_publish(&buf[i]);
    } while (n == 8);  /* 若返回满额，可能还有剩余，继续拉 */
}

void event_dispatcher_tick(void) {
    /* 从各聚合拉取待发布事件（循环直到清空）*/
    pull_and_publish((PullFn)wash_session_pull_events,    &g_wash_session);
    pull_and_publish((PullFn)op_mode_pull_events,         &g_op_mode);
    pull_and_publish((PullFn)alarm_registry_pull_events,  &g_alarm_registry);

    /* 统一分发出队（依序调用所有订阅者）*/
    event_bus_dispatch_all();
}
```

---

### 15.6 线程架构

```c
/* === platform/safety_thread.c：EStop 专用高优先级线程 === */

/*
 * EStop 双路径设计（safety_thread 为 OperationalMode 的权威更新者）：
 * ───────────────────────────────────────────────────────────────────────
 * 触发（estop=true）：
 *   ① safety_thread（快速通道）：
 *        device_stop_all_actuators()          → 硬件立即停机
 *        op_mode_on_estop_triggered()         → OperationalMode 进入 EXCEPTION（同步，持锁）
 *        event_bus_publish(EStopTriggered)    → 异步通知云端/日志
 *   ② main_loop sensor_poll（慢速 ~20ms 内）：
 *        alarm_registry_on_sensor_updated(ALARM_ESTOP, ACTIVE)
 *        → AlarmRegistry 创建 AlarmInstance，发布 AlarmTriggered(ALARM_ESTOP)
 *   ③ cmd_handler_on_alarm_triggered(ALARM_ESTOP)：
 *        跳过 op_mode_on_alarm_triggered（①已处理，保证幂等）
 *        只做云端上报
 *
 * 清除（estop=false）：
 *   ① safety_thread（快速通道）：
 *        op_mode_on_estop_cleared()           → 更新 estop_active=false（持锁）
 *   ② main_loop sensor_poll（慢速 ~20ms 内）：
 *        alarm_registry_on_sensor_updated(ALARM_ESTOP, CLEARED)
 *        → AlarmRegistry 清除 AlarmInstance，发布 AlarmCleared(ALARM_ESTOP)
 *   ③ cmd_handler_on_alarm_cleared(ALARM_ESTOP)：
 *        跳过 op_mode_on_estop_cleared（①已处理，保证幂等）
 *        只做云端上报
 *
 * 约束与已知 trade-off：
 *   · g_op_mode_mutex 保护 g_op_mode 的所有跨线程访问
 *   · op_mode_on_estop_triggered/cleared 自身保证幂等（重复调用无副作用）
 *   · OperationalMode 职责：estop_active 标志的即时维护（决定 Recover 可用性）
 *   · AlarmRegistry 职责：ALARM_ESTOP 实例的生命周期记录（历史日志、云端上报）
 *
 *   ⚠ 已知限制（接受的 trade-off）：
 *   EStop 触发后约 ≤20ms 窗口内，OperationalMode.estop_active = true，
 *   但 AlarmRegistry 尚未创建 ALARM_ESTOP 实例（sensor_poll 尚未运行）。
 *   此窗口内两者状态短暂不一致。
 *   当前不构成实际问题：Recover 命令合法性检查用 op_mode_is_estop_active()，
 *   不查 AlarmRegistry，因此不受影响。
 *
 *   备选设计（消除窗口期）：
 *   safety_thread 检测到 EStop 后，持同一把锁顺便调用
 *   alarm_registry_trigger(ALARM_ESTOP) / alarm_registry_clear(ALARM_ESTOP)，
 *   AlarmInstance 创建与 OperationalMode 状态更新同步完成，无窗口期。
 *   代价：ALARM_ESTOP 从 AUTO_STATIC 变为"safety_thread 直接驱动"的特殊类，
 *   sensor_poll 不再负责 EStop，分类整洁性降低。
 *   如将来窗口期引发问题，可按此方向切换，无需修改 AlarmRegistry 接口。
 * ───────────────────────────────────────────────────────────────────────
 */

static void *safety_thread_func(void *arg) {
    struct sched_param param = { .sched_priority = 90 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    while (1) {
        bool estop = gpio_read(ESTOP_GPIO_PIN);
        if (estop != g_last_estop_state) {
            g_last_estop_state = estop;
            if (estop) {
                device_stop_all_actuators();          /* 硬件停机，无需持锁 */
                pthread_mutex_lock(&g_op_mode_mutex);
                op_mode_on_estop_triggered(&g_op_mode); /* 聚合状态更新，持锁保护 */
                pthread_mutex_unlock(&g_op_mode_mutex);
                /* 发布 EStopTriggered 事件（异步通知，不影响停机时序）*/
                DomainEvent evt = { .type = EVT_ESTOP_TRIGGERED };
                event_bus_publish(&evt);
            } else {
                pthread_mutex_lock(&g_op_mode_mutex);
                op_mode_on_estop_cleared(&g_op_mode);
                pthread_mutex_unlock(&g_op_mode_mutex);
            }
        }
        usleep(5000);  /* 5ms 轮询，最坏响应 5ms */
    }
}

/* === platform/main_loop.c：主循环线程，20ms tick === */

static void *main_loop_func(void *arg) {
    while (1) {
        uint64_t tick_start = get_monotonic_ms();

        /* ① 协议收包 */
        can_driver_poll();
        modbus_driver_poll();

        /* ② 传感器数据 → 报警检测（直接调用，不经事件总线） */
        sensor_poll_and_update(&g_alarm_registry);

        /* ③ 洗车执行 tick */
        wash_session_tick(&g_wash_session);

        /* ④ 从聚合拉取事件 + 分发 */
        event_dispatcher_tick();

        /* ⑤ 云端消息处理 */
        cloud_port_poll();

        /* 保持 20ms 周期 */
        uint64_t elapsed = get_monotonic_ms() - tick_start;
        if (elapsed < 20) usleep((20 - elapsed) * 1000);
    }
}

/*
 * Main Loop 访问 g_op_mode 的所有路径（cmd_handler_xxx / op_mode_on_xxx）
 * 均需持 g_op_mode_mutex，与 safety_thread 的 EStop 路径互斥。
 */
```

---

### 15.7 依赖方向强制规则（编译检查）


| 层                 | 可 include                              | 禁止 include                                    |
| ----------------- | -------------------------------------- | --------------------------------------------- |
| `domain/`         | `domain/shared/`、标准库                   | `infrastructure/`、`platform/`、`application/`  |
| `infrastructure/` | `domain/shared/`（接口类型）、`platform/`、标准库 | `domain/` 具体实现、`application/`                 |
| `platform/`       | `domain/shared/`（事件类型）、标准库、POSIX       | `domain/` 实现、`infrastructure/`、`application/` |
| `application/`    | 全部                                     | —                                             |


通过 CMakeLists.txt 的 `target_include_directories` 分层配置 include 路径，使违反规则的 include 在编译时报错。

*文档持续更新，下一阶段：编码实现（从 domain/ 层开始）*
