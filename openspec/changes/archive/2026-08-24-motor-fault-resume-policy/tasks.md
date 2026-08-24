## 1. 配置与查询

- [x] 1.1 在 `motor_motor_cfg_t` 增加 `uint32_t confirm_faults`（默认 0 = 全部可续动），Doxygen 说明需确认 / 可续动、fatal 硬覆盖
- [x] 1.2 bind 拒绝未定义故障码位；`DRIVER_PORT_FATAL` 位即使置 1 也不得按可续动放行
- [x] 1.3 端口增加 `motor_exec_fault_requires_confirm(exec, motor, code)` 只读查询

## 2. 运动命令路径

- [x] 2.1 `run`/`home`：非 fatal FAULT 且当前码可续动时，同一把锁内两步 recover，成功则继续本次启动
- [x] 2.2 可续动内清失败：保持 FAULT 并拒绝本次命令
- [x] 2.3 需确认码：保持 `MOTOR_CMD_REJECT_FAULT`，不内清
- [x] 2.4 tick 不得把可续动 FAULT 自行变为 STOPPED；hold/ESTOP/fatal/看门狗不走可续动内清

## 3. 测试

- [x] 3.1 默认 `confirm_faults=0`：过流 FAULT 后不经 recover 的 `run` 可受理（复位成功）
- [x] 3.2 仅过温列入确认表：过流可续动、过温拒令；显式 recover 后过温可再 run
- [x] 3.3 可续动复位失败则 `run` 拒绝且仍 FAULT；仅 tick 不自复
- [x] 3.4 fatal 与 hold/ESTOP 在位图为 0 时仍拒令
- [x] 3.5 未定义位 bind 失败

## 4. 文档与门禁

- [x] 4.1 更新 `机构控制模式模块设计.md`：本轴按码需确认/可续动、默认可续动、不绑报警、无清障扫描
- [x] 4.2 运行 `python3 scripts/gen_doc_index.py` 后执行 `./scripts/check_all.sh`（无 cmake 则 `--skip-tests`）
