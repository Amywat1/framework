# Storage 端口与方案资产模块设计

**版本**：v1.0  
**状态**：已落地（param_store + deploy_store + engine_program_loader + JSON 适配器 + manifest 校验）  
**最后同步代码**：2026-07-14（`ports/outbound/storage`、`adapters/outbound/storage/json`、`engine_program_manifest`）  
**适用范围**：`ports/outbound/storage/`、`adapters/outbound/storage/json/`、`domain/program_engine/model/engine_program_manifest.*`、`application/engine_session.*`、项目编排器  
**架构基线**：Ports & Adapters + 存储类型分离 + 方案资产完整性校验  
**关键词**：param_store、deploy_store、engine_program_loader、engine_program_json、manifest、SHA256

---

## 1. 设计目标与核心理念

存储层把运行期参数、部署期配置和洗车方案资产分成三类能力。业务通过 port 访问数据，不直接依赖 JSON 文件、cJSON、路径宏或 manifest 格式。

### 1.1 设计目标

- **按生命周期分离**：运行期参数可写，部署配置只读，洗车方案只加载不修改。
- **端口隔离格式**：上层依赖 `param_store` / `deploy_store` / `engine_program_loader_port`，JSON 是可替换适配器。
- **路径由项目配置注入**：JSON 文件路径由 `project_configure_storage()` 调用 `json_*_store_configure(path)` 注入，编译宏只作为默认 fallback。
- **方案完整性校验**：wash worker 在加载方案前校验 `*.manifest.json` 中的 SHA256 和 size。
- **错误显式返回**：文件缺失、JSON 解析失败、键不存在、manifest 不匹配都有明确返回值。

### 1.2 存储类型对比

| 类型 | 端口 | 适配器 | 写接口 | 典型内容 |
|------|------|--------|--------|----------|
| 运行期参数 | `param_store` | `json_param_store` | `set` + `save` | 运行模式、计数、校准值 |
| 部署期配置 | `deploy_store` | `json_deploy_store` | 无 | SN、站点、MQTT topic、服务器地址 |
| 洗车方案 | `engine_program_loader_port` | `engine_program_json` | 无 | phase/lane/step/interlock 配置 |
| 方案 manifest | 直接 domain API | `engine_program_manifest` | 无 | JSON 文件 size + SHA256 |

---

## 2. 分层归属与依赖方向

### 2.1 模块落位

| 层次 | 路径 | 职责 |
|------|------|------|
| **运行参数端口** | `ports/outbound/storage/param_store.h` | KV load/save/get/set 契约 |
| **部署配置端口** | `ports/outbound/storage/deploy_store.h` | 只读 load/get 契约 |
| **方案加载端口** | `ports/outbound/storage/engine_program_loader_port.*` | `engine_program_t` 加载抽象 |
| **JSON 参数适配器** | `adapters/outbound/storage/json/json_param_store.*` | 基于 cJSON 的可写 KV 文件 |
| **JSON 部署适配器** | `adapters/outbound/storage/json/json_deploy_store.c` | 基于 cJSON 的只读部署文件 |
| **JSON 方案加载器** | `adapters/outbound/storage/json/engine_program_json.*` | JSON → `engine_program_t` |
| **manifest 校验** | `domain/program_engine/model/engine_program_manifest.*` | SHA256 + manifest 文件比对 |
| **消费方** | `services/param/svc_param.*` | 运行期参数具名 API |
| **消费方** | `wash_orchestrator` | 启动洗车前校验并加载方案 |

### 2.2 依赖方向

```text
application / services
        │ storage ports
        ▼
ports/outbound/storage
        ▲
        │ register ops
adapters/outbound/storage/json
        │
        ├─ third_party/cJSON
        └─ 标准 C 文件 I/O

wash_orchestrator
        ├─ engine_program_manifest_verify()
        └─ engine_program_load()
```

**依赖禁令**：

- 业务模块不得直接 `fopen` 参数/部署 JSON。
- `ports/outbound/storage` 不得 include cJSON 或具体适配器头。
- `json_param_store` / `json_deploy_store` 不得包含业务键名常量。
- 方案 loader 不得执行洗车流程，只构建并校验模型。

---

## 3. 运行期参数存储

`param_store_ops_t`：

```c
typedef struct {
    sw_err_t (*load)(void);
    sw_err_t (*save)(void);
    sw_err_t (*get)(const char *key, char *buf, size_t buf_size);
    sw_err_t (*set)(const char *key, const char *val);
} param_store_ops_t;
```

### 3.1 JSON 适配器行为

| API | 行为 |
|-----|------|
| `load()` | 释放旧树，创建空对象，再读取 `PARAM_STORE_JSON_FILE_PATH` |
| `save()` | 序列化当前内存树并写回文件 |
| `get()` | 支持顶层 String / Number，统一以字符串返回 |
| `set()` | 已存在 Number 则 `atof` 更新；否则写 String；不自动 save |

文件不存在、空文件或 JSON 非法返回 `SW_ERR_STORAGE`。`bootstrap` 中 `svc_param_init()` 返回 `SW_ERR_STORAGE` 时允许继续，由业务默认值兜底。

### 3.2 JSON 文件形态

文件为扁平 JSON 对象，键为参数名，值为字符串或数值：

```json
{
  "deviceName": "M8-001",
  "maxSpeed": 120,
  "mode": "auto"
}
```

- 不支持嵌套对象作为 KV 值。
- 数组、布尔等类型在 `get()` 时视为键不存在，返回 `SW_ERR_PARAM`。
- 新增键统一以 cJSON String 写入。

### 3.3 API 行为细节

| API | 条件 | 行为 / 返回 |
|-----|------|-------------|
| `load()` | 文件不存在 | 内存保留空对象，返回 `SW_ERR_STORAGE` |
| `load()` | JSON 合法 | 解析替换 `s_root`，返回 `SW_OK` |
| `load()` | JSON 非法或空文件 | 内存保留空对象，返回 `SW_ERR_STORAGE` |
| `save()` | `s_root == NULL` | 返回 `SW_ERR_STORAGE` |
| `save()` | 文件不可写 | 打 ERROR 日志，返回 `SW_ERR_STORAGE` |
| `get()` | 参数非法 / 未 load / 键不存在 | 返回 `SW_ERR_PARAM` |
| `get()` | String | 拷贝字符串到 `buf`，截断并保证 `\0` |
| `get()` | Number | 用 `snprintf("%g")` 转字符串 |
| `set()` | `key` / `val` 为 NULL | 返回 `SW_ERR_PARAM` |
| `set()` | `s_root == NULL` | 静默无操作，返回 `SW_OK` |
| `set()` | 已存在 Number | `atof(val)` 更新数值 |
| `set()` | 已存在 String | 更新字符串 |

> 头文件注释曾写“文件不存在返回 SW_OK”，当前实现为 `SW_ERR_STORAGE`；调用方应以实现为准处理默认值。

### 3.4 路径注入

JSON 适配器注册与路径配置分离：

```text
wiring()
    └─ json_param_store_register()

project_configure_storage()
    └─ json_param_store_configure("/path/to/params.json")
```

若项目未显式调用 `json_param_store_configure()`，适配器才回退到编译期 `PARAM_STORE_JSON_FILE_PATH`。Demo 和真机项目应优先在 `project_configure_storage()` 注入路径，测试可继续使用编译宏作为默认值。

### 3.5 `svc_param` 边界

`param_store` 是原始 KV 端口，`services/param/svc_param.*` 是带业务语义的具名访问层。业务优先依赖 `svc_param`，避免在多处散落参数键名。项目负责定义键名语义、默认值策略和参数变更后的保存时机。

---

## 4. 部署期配置存储

`deploy_store_ops_t`：

```c
typedef struct {
    sw_err_t (*load)(void);
    sw_err_t (*get)(const char *key, char *buf, size_t buf_size);
} deploy_store_ops_t;
```

部署配置只读，没有 `set` / `save`。适配器通过 `json_deploy_store_configure(path)` 注入路径；未显式配置时才回退到编译期 `DEPLOY_STORE_JSON_FILE_PATH`，支持顶层 String / Number。

### 4.1 JSON 适配器行为

| 场景 | 返回 |
|------|------|
| 文件不存在 | `load()` 返回 `SW_ERR_STORAGE` |
| JSON 合法 | `load()` 返回 `SW_OK` |
| JSON 非法或空文件 | `load()` 返回 `SW_ERR_STORAGE` |
| 未 load 或键不存在 | `get()` 返回 `SW_ERR_PARAM` |
| String / Number 键 | `get()` 返回 `SW_OK`，输出字符串 |

`bootstrap_load_storage()` 在 `project_configure_storage()` 后调用 `deploy_store.load()`；`SW_ERR_STORAGE` 被允许继续，具体 provider 可在后续配置或初始化时处理缺省配置。

---

## 5. 洗车方案加载端口

`engine_program_loader_port` 屏蔽 JSON 或未来二进制格式：

```c
typedef struct {
    engine_program_t *(*load)(const char *path, char *err, unsigned errsz);
} engine_program_loader_ops_t;
```

| API | 行为 |
|-----|------|
| `engine_program_loader_register(ops)` | 注册全局加载器，`ops->load` 非空才生效 |
| `engine_program_load(path, err, errsz)` | 调用已注册 loader；未注册返回 NULL 并写错误描述 |
| `engine_program_json_register_loader()` | 将 JSON loader 注册为实现 |

JSON loader 支持：

- 从 JSON 字符串或文件加载。
- 解析 `program.schema_version == "1.0"`。
- 构建 `engine_program_t`、编译表达式、执行语义校验。
- 失败时返回 NULL，并尽量写入错误描述。

---

## 6. 方案 Manifest 校验

`engine_program_manifest` 在 `domain/wash/model` 中实现，因为它约束的是洗车方案资产完整性，不是通用 KV 存储。

### 6.1 路径规则

```text
xxx.json → xxx.manifest.json
```

`engine_program_manifest_path_from_json()` 只接受 `.json` 后缀，输出缓冲不足或参数非法返回 `false`。

### 6.2 Manifest 格式

运行期校验当前只读取两个字段：

```json
{
  "sha256": "64 hex chars",
  "size": 1234
}
```

| 字段 | 说明 |
|------|------|
| `sha256` | JSON 文件原始字节的 SHA256，小写 64 位 hex |
| `size` | JSON 文件字节数 |

### 6.3 校验行为

| 场景 | 返回 |
|------|------|
| 路径为空 | `SW_ERR_PARAM` |
| JSON 文件无法读取 | `SW_ERR_PARAM` |
| manifest 文件无法读取或 JSON 非法 | `SW_ERR_PARAM` |
| manifest 缺字段 | `SW_ERR_PARAM` |
| sha256 格式错误 | `SW_ERR_CRC` |
| size 不匹配 | `SW_ERR_CRC` |
| sha256 不匹配 | `SW_ERR_CRC` |
| 全部匹配 | `SW_OK` |

`wash_orchestrator` 在 worker 启动阶段先推导 manifest 路径，再调用 `engine_program_manifest_verify()`，校验通过后才 `engine_program_load()`。

---

## 7. 启动与接入顺序

推荐顺序：

```text
wiring()
    ├─ json_param_store_register()
    ├─ json_deploy_store_register()
    └─ engine_program_json_register_loader()

project_configure_storage()
    ├─ json_param_store_configure(param_path)
    └─ json_deploy_store_configure(deploy_path)

bootstrap_load_storage()
    ├─ svc_param_init()
    │    └─ param_store.load()
    └─ deploy_store.load()

bootstrap_init()
    └─ project_register_runtime_tasks()

wash_orchestrator worker
    ├─ engine_program_manifest_path_from_json()
    ├─ engine_program_manifest_verify()
    ├─ engine_program_load()
    └─ engine_load_program()
```

当前 Demo wiring 注册 param/deploy store；JSON 路径由 Demo `project_configure_storage()` 注入。方案 loader 在相关测试和项目 wiring 中按需注册。

---

## 8. 线程安全与并发

| 模块 | 线程安全策略 |
|------|--------------|
| `json_param_store.load` | 全程持 mutex，含文件读与树重建 |
| `json_param_store.save` | 持锁取 JSON 字符串，写文件在锁外进行 |
| `json_param_store.get/set` | 持锁访问 `s_root` |
| `json_deploy_store` | 单 mutex 保护 cJSON 树 |
| `engine_program_loader_port` | 单例指针，无锁；应在启动期注册 |
| `engine_program_manifest` | 无全局状态，函数级局部内存 |

约束：

- 存储 adapter 注册应发生在启动期，不在运行期热切换。
- 参数 `set()` 不自动落盘，调用方负责批量写后 `save()`。
- 业务层应避免在持其他全局锁时调用 `load()`。
- 多进程同时写同一参数 JSON 不受支持。
- manifest 校验读取的是文件原始字节，构建产物必须与运行部署文件一致。

---

## 9. 错误码与日志

| 场景 | 错误码 | 日志 |
|------|--------|------|
| 参数文件不存在 | `SW_ERR_STORAGE` | WARN |
| 参数 JSON 解析失败 | `SW_ERR_STORAGE` | 无额外 ERROR |
| 参数文件不可写 | `SW_ERR_STORAGE` | ERROR |
| 参数键不存在 / 参数非法 | `SW_ERR_PARAM` | 无 |
| 部署文件不存在 | `SW_ERR_STORAGE` | WARN |
| 部署键不存在 / 参数非法 | `SW_ERR_PARAM` | 无 |
| loader 未注册 | 返回 NULL | 写入错误描述 |
| manifest 读取/格式错误 | `SW_ERR_PARAM` | 写入错误描述 |
| manifest size/sha256 不匹配 | `SW_ERR_CRC` | 写入错误描述 |
| 注册完成 / 加载成功 / 保存成功 | `SW_OK` 或无返回 | INFO |

---

## 10. 典型用法

### 10.1 参数读写

```c
const param_store_ops_t *ps = param_store_get_ops();
char buf[64];

if ((ps != NULL) && (ps->get("mode", buf, sizeof(buf)) == SW_OK)) {
    /* 使用 buf */
}

if ((ps != NULL) && (ps->set("mode", "auto") == SW_OK)) {
    (void)ps->save();
}
```

### 10.2 部署配置读取

```c
const deploy_store_ops_t *ds = deploy_store_get_ops();
char topic[128];

if ((ds != NULL) && (ds->get("topicPropertyUp", topic, sizeof(topic)) == SW_OK)) {
    /* 使用 topic */
}
```

### 10.3 方案加载

```c
char err[160];
engine_program_t *program = engine_program_load(path, err, sizeof(err));
```

---

## 11. 测试覆盖

| 测试 | 覆盖 |
|------|------|
| `tests/adapters/test_json_param_store.c` | 参数 JSON load/save/get/set |
| `tests/services/test_svc_param.c` | `svc_param` 对 param_store 的封装 |
| `tests/adapters/test_json_deploy_store.c` | 部署 JSON load/get |
| `tests/adapters/test_engine_program_json.c` | JSON 方案加载器、loader port、sim IO 闭环 |
| `tests/domain/test_wash_engine.c` | engine 模型/表达式/运行时 |
| `tests/application/test_wash_orchestrator.c` | orchestrator 加载方案并驱动 engine |

验证命令：

```bash
cmake --build build-native -j4
ctest --test-dir build-native --output-on-failure
```

---

## 12. 扩展指南

### 12.1 新增存储后端

1. 保持 port 契约不变。
2. 新增 `adapters/outbound/storage/<backend>/...`。
3. 在项目 wiring 中注册新 ops。
4. 保持错误码语义与 JSON 适配器一致。

### 12.2 新增业务参数

1. 在 `svc_param` 或业务模块定义具名 getter/setter。
2. 内部调用 `param_store_get_ops()->get/set`。
3. 在参数变更流程末尾显式调用 `save()`。
4. 不要在 `json_param_store.c` 增加业务键名常量。

### 12.3 新增方案格式

1. 实现新的 `engine_program_loader_ops_t.load`。
2. 构建完整 `engine_program_t` 并调用语义校验。
3. 明确资产完整性校验方式，可以复用 manifest 或提供等价机制。
4. application 层仍只调用 `engine_program_load()`。

### 12.4 禁止的扩展方式

- 在业务模块中直接读写参数/部署 JSON 文件。
- 运行期修改 deploy store。
- 在 JSON 适配器里写入产品业务键名常量。
- 在 `set()` 内隐式自动 `save()`。
- 通过 param_store 存储大二进制或嵌套结构。
- 与 deploy_store 混用同一 JSON 文件。
- 跳过 manifest 校验直接加载生产方案。

---

## 13. 当前落地状态

| 能力 | 状态 |
|------|------|
| `param_store` 端口 | 已落地 |
| `json_param_store` 适配器 | 已落地 |
| `svc_param` 业务层 | 已落地 |
| `deploy_store` 端口 | 已落地 |
| `json_deploy_store` 适配器 | 已落地 |
| `engine_program_loader_port` | 已落地 |
| `engine_program_json` loader | 已落地 |
| `engine_program_manifest` | 已落地 |
| Demo param/deploy wiring | 已落地 |

---

## 14. 相关文档

- `doc/module-design/domain/方案引擎模块设计.md` — engine program 模型与运行时
- `doc/module-design/runtime/Runtime模块设计.md` — wiring、bootstrap 与 worker 启动顺序
- `tests/reports/json_param_store.md` — 参数存储单元测试报告
