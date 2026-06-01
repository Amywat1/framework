# ??????

## ?????

```text
apps/wash_controller/
include/core/{bootstrap,runtime,scheduler,tick}/
include/ports/{hal,storage,application/inbound,application/outbound}/
include/adapters/{background,os,hal/sim_hw,ui/cli,config,outbound}/
src/core/?src/adapters/?src/application/{services,use_cases/internal}/
src/domain/
assets/configs/
tests/support/
```

## ???????

- startup ? apps/?core/bootstrap
- coordinators ???? ? core/runtime?core/tick
- domain/ports ? ports/hal?ports/storage
- platform ? adapters/os/linux?adapters/hal/sim_hw?adapters/ui/cli
- configs ? assets/configs
- ???? ? adapters/background?? alarm_evaluator?alarm_detect_job?alarm_monitor?
- test_support ? tests/support
- application/ports ? ports/application/

**?????**?include/platform?src/platform?application/coordinators?application/jobs?application/ports????application/dto????

**??????/?**??? CMake??device_runtime?runtime_event_recorder?runtime_result_projection??? control_tick/scheduler_runtime_port?process_formal_command/process_wash_trigger ????

## ?? application/

- services/command_dispatch
- use_cases/line_command?wash_control?query_wash_session_status
- use_cases/internal/device_state_blocked_reasons.h

## ????

| ?? | ?? |
|------|------|
| system_context | control_context |
| process_wash_trigger | wash_control |
| process_formal_command | line_command |
| runtime_event_recorder | control_outcome_recorder |

## ????

- `system_context` ????????? `control_context`?CMake ???????????

## ??

?????????

```bash
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure
```
