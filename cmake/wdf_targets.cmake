# wdf_targets.cmake — 通用框架的可复用构建目标
#
# 设计取向：
#   框架核心以 INTERFACE 库导出，而不是 STATIC 库。原因是同一份框架源在不同
#   目标下需要不同的编译定义——例如项目的 smoke 目标会为 fluid_path.c 定义
#   FLUID_PATH_UNIT_TEST 以剥离周期任务线程，而 sim / 真机目标不定义。
#   STATIC 库只编译一次，无法同时满足这些差异；INTERFACE 库把源文件与包含
#   目录传递给消费方，由消费方按自己的宏编译，行为与项目手写源列表完全一致。
#
#   这样做的收益不是"少编译一次"，而是把源清单的维护权收回框架内部：
#   框架增删或移动文件，项目只需重新配置，不必同步修改自己的源列表。
#
# 用法（项目侧）：
#   include(${FW_ROOT}/cmake/wdf_targets.cmake)
#   target_link_libraries(my_app PRIVATE wdf_runtime wdf_domain wdf_storage_json)
#
# 分层目标（依赖方向与 architecture/01 一致，逐层向下传递）：
#   wdf_common      错误码、日志、时间、追踪上下文、点表模型、基础工具
#   wdf_ports       端口注册表与端口内自带实现
#   wdf_domain      领域规则（依赖 common + ports）
#   wdf_runtime     启动编排、事件总线、调度器（依赖 common）
#   wdf_application 跨领域编排、桥接、投影（依赖 domain + runtime）
#   wdf_asset_contract 必需资产启动期校验（依赖 cloud + program_engine）
#   wdf_observation_bridge 框架事件转观测记录（依赖 observability）
#   wdf_cloud       通用云点位模型与变化检测
#   wdf_services    参数服务等共享服务
#   wdf_observability 观测记录与黑匣子
#
# 可选适配器目标（按需 link，不进入上面的分层依赖）：
#   wdf_storage_json     JSON 参数/部署/方案存储适配器
#   wdf_hal_sim          IO / 语音仿真后端
#   wdf_hal_engine_sim   方案引擎 IO / 执行器仿真后端
#   wdf_hal_components   ADC 门控、传感器滤波、VFD 管理器
#   wdf_point_table_json 点位表的 JSON 编解码
#   wdf_cloud_json       物模型属性 JSON 与 property_port 安装
#   wdf_cjson            随框架分发的 cJSON
#
# 注意：vendor provider（mcc / snack）仍由 framework/CMakeLists.txt 的
#       WDF_ENABLE_* 选项以 STATIC 库形式提供，它们有外部 SDK 依赖，
#       不适合作为无条件导出的 INTERFACE 源。

if(TARGET wdf_common)
    return()
endif()

get_filename_component(WDF_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# ---------------------------------------------------------------------------
# 内部辅助：定义一个 INTERFACE 源目标
#   _wdf_add_interface_lib(<name> SOURCES <相对框架根的源> [DEPENDS <目标>...])
# ---------------------------------------------------------------------------
function(_wdf_add_interface_lib name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})

    add_library(${name} INTERFACE)

    set(_abs_sources "")
    foreach(src IN LISTS ARG_SOURCES)
        if(NOT EXISTS "${WDF_ROOT_DIR}/${src}")
            message(FATAL_ERROR "wdf_targets: ${name} 引用的源文件不存在: ${src}")
        endif()
        list(APPEND _abs_sources "${WDF_ROOT_DIR}/${src}")
    endforeach()

    target_sources(${name} INTERFACE ${_abs_sources})
    target_include_directories(${name} INTERFACE ${WDF_ROOT_DIR})

    if(ARG_DEPENDS)
        target_link_libraries(${name} INTERFACE ${ARG_DEPENDS})
    endif()
endfunction()

# ---------------------------------------------------------------------------
# wdf_cjson — 随框架分发的 JSON 解析器
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_cjson
    SOURCES
        third_party/cJSON/cJSON.c
)

# ---------------------------------------------------------------------------
# wdf_common — 最底层，不依赖任何框架上层，也不依赖任何外部格式
#
# 原先 point_table 的 JSON 编解码在此，使 wdf_common 传递 wdf_cjson——最底层
# 绑定了一种序列化格式，且让「domain 不解析序列化格式」（R9b）失去基础：
# domain 依赖 common，而 common 自己 include cJSON。编解码已移入
# wdf_point_table_json，wdf_common 现在零外部依赖。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_common
    SOURCES
        common/asset_version.c
        common/log.c
        common/sw_mutex.c
        common/time_util.c
        common/trace_context.c
        common/pulse_out.c
        common/util_crc.c
        common/util_fifo.c
        common/point_table/point_table.c
)

# ---------------------------------------------------------------------------
# wdf_point_table_json — 点位表的 JSON 编解码（可选适配器）
#
# 需要另一种编码时在同目录并列新增实现，点位表模型（wdf_common）不必改动。
# 不接云、不用 JSON 存储的项目可以完全不链接它，也就不链接 cJSON。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_point_table_json
    SOURCES
        adapters/outbound/serialization/json/point_table_from_json.c
        adapters/outbound/serialization/json/point_table_to_json.c
    DEPENDS
        wdf_common
        wdf_cjson
)

# ---------------------------------------------------------------------------
# wdf_ports — 端口注册表与端口内自带实现
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_ports
    SOURCES
        ports/port_contract.c
        ports/port_registry_hal.c
        ports/port_registry_safety.c
        ports/port_registry_cloud.c
        ports/port_registry_infra.c
        ports/outbound/storage/engine_program_loader_port.c
    DEPENDS
        wdf_common
)

# ---------------------------------------------------------------------------
# wdf_runtime — 启动编排、事件总线、调度器
#
# bootstrap 会调用 domain / application 的初始化入口，但那些符号由消费方
# 一并链接（同一可执行文件内），此处不制造 runtime → application 的目标
# 依赖，以免与 architecture/01 的依赖方向冲突。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_runtime
    SOURCES
        runtime/bootstrap/bootstrap.c
        runtime/event_bus/event_bus.c
        runtime/scheduler/scheduler.c
        runtime/scheduler/periodic_task.c
        runtime/scheduler/thread_registry.c
    DEPENDS
        wdf_common
)

# ---------------------------------------------------------------------------
# wdf_domain — 领域核心规则
#
# 只含命令裁决、报警与遥测读模型这类每个设备项目都要用的部分，不含设备控制
# 模式和方案引擎——那两者分别拆为 wdf_device_control 与 wdf_program_engine，
# 因为它们各自要求项目提供 motor provider / 方案资产，最小接入（例如框架
# 自带 demo）并不需要，捆绑进核心会造成链接期缺符号。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_domain
    SOURCES
        domain/op_mode/operational_mode.c
        domain/safety/alarm_registry/alarm_registry.c
        domain/telemetry/device_snapshot.c
    DEPENDS
        wdf_common
        wdf_ports
)

# ---------------------------------------------------------------------------
# wdf_device_control — 设备控制通用模式（运动、旋转、互锁、流体路径）
#
# motor_axis 会调用 hal_motor_exec_port 声明的函数，这些符号由项目选择的
# motor provider（例如 wdf_hal_motor_exec_mcc）提供，必须一并链接。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_device_control
    SOURCES
        domain/device_control/model/actuator_events.c
        domain/device_control/patterns/motor_axis.c
        domain/device_control/patterns/fluid_path.c
    DEPENDS
        wdf_common
        wdf_ports
        wdf_runtime
)

# ---------------------------------------------------------------------------
# wdf_program_engine — 洗车方案引擎（模型、表达式、tick 运行时）
#
# 纯领域规则：不含文件读取与 JSON 解析，方案资产的反序列化与完整性校验
# 都在 wdf_storage_program_json 里。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_program_engine
    SOURCES
        domain/program_engine/engine/engine.c
        domain/program_engine/engine/engine_expr.c
        domain/program_engine/engine/engine_actuator.c
        domain/program_engine/engine/engine_io.c
        domain/program_engine/engine/engine_var.c
        domain/program_engine/engine/engine_profile.c
        domain/program_engine/model/engine_model.c
        domain/program_engine/model/engine_program_validate.c
    DEPENDS
        wdf_common
        wdf_ports
)

# ---------------------------------------------------------------------------
# wdf_application — 跨领域编排、桥接、投影
#
# 只含依赖 domain 核心的编排：命令网关、副作用路由、自检、恢复、遥测投影，
# 以及报警与运行模式的事件桥接。依赖方案引擎的 engine_session 和依赖云模块的
# report_scheduler 各自独立成目标，理由同 wdf_domain 的拆分。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_application
    SOURCES
        application/command_gateway.c
        application/side_effect_router.c
        application/telemetry_projection.c
        application/orchestrators/recovery_service.c
        application/orchestrators/safety_session_coordinator.c
        application/bridges/alarm_bridge.c
        application/bridges/op_mode_bridge.c
    DEPENDS
        wdf_domain
        wdf_runtime
)

# ---------------------------------------------------------------------------
# wdf_observation_bridge — 框架事件转观测记录
#
# 独立成目标而非并入 wdf_application：它是 observability 的唯一框架侧接入点，
# 不接观测的项目不该被迫链接 observability。
#
# 只有需要这套通用事件投影的项目才链接本目标并在 init_adapters 中调
# observation_event_bridge_init()；bootstrap 不引用该符号。自带事件投影的项目
# （有自己的事件编码与载荷）应只链接 wdf_observability。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_observation_bridge
    SOURCES
        application/bridges/observation_event_bridge.c
    DEPENDS
        wdf_application
        wdf_observability
)

# ---------------------------------------------------------------------------
# wdf_asset_contract — 必需资产启动期校验
#
# 独立成目标而不并入 wdf_application：它要探测报警目录、云物模型与方案引擎 IO
# 目录三处资产，并入会让最小接入被迫链接 cloud 与 program_engine。项目按自己
# 声明了哪些资产决定是否链接，与 wdf_report_scheduler 的拆分理由相同。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_asset_contract
    SOURCES
        application/asset_contract.c
    DEPENDS
        wdf_application
        wdf_cloud
        wdf_program_engine
)

# ---------------------------------------------------------------------------
# wdf_engine_session — 方案引擎会话 worker（独立线程驱动 engine tick）
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_engine_session
    SOURCES
        application/engine_session/engine_session.c
    DEPENDS
        wdf_application
        wdf_program_engine
)

# ---------------------------------------------------------------------------
# wdf_report_scheduler — 云端上报调度（周期 + 事件触发策略）
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_report_scheduler
    SOURCES
        application/orchestrators/report_scheduler.c
    DEPENDS
        wdf_application
        wdf_cloud
)

# ---------------------------------------------------------------------------
# wdf_cloud — 通用云点位模型、上下行转换与变化检测
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_cloud
    SOURCES
        domain/cloud/cloud_model.c
        domain/cloud/cloud_point_dispatch.c
        domain/cloud/cloud_point_validate.c
        domain/cloud/cloud_point_watcher.c
    DEPENDS
        wdf_common
        wdf_ports
        wdf_runtime
)

# ---------------------------------------------------------------------------
# wdf_cloud_json — 物模型的属性 JSON 编解码与 property_port 安装（可选适配器）
#
# 与 wdf_cloud 分开：property_port 的契约参数是 JSON 载荷，实现它必须解析
# JSON。留在 wdf_cloud 会让点位模型与语义分派绑定一种传输格式，也会使
# cloud/ 依赖 adapters/（违反 R12）。换协议时并列新增实现即可。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_cloud_json
    SOURCES
        adapters/outbound/cloud/cloud_model_json.c
        adapters/outbound/cloud/cloud_point_json.c
    DEPENDS
        wdf_cloud
        wdf_point_table_json
)

# ---------------------------------------------------------------------------
# wdf_services — 参数服务等共享服务
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_services
    SOURCES
        services/param/svc_param.c
    DEPENDS
        wdf_common
        wdf_ports
        wdf_domain
)

# ---------------------------------------------------------------------------
# wdf_observability — 观测记录与黑匣子
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_observability
    SOURCES
        observability/core/observation.c
        observability/recorder/blackbox_recorder.c
    DEPENDS
        wdf_common
)

# ---------------------------------------------------------------------------
# wdf_storage_json — 参数与部署配置的 JSON 存储适配器
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_storage_json
    SOURCES
        adapters/outbound/storage/json/json_param_store.c
        adapters/outbound/storage/json/json_deploy_store.c
    DEPENDS
        wdf_common
        wdf_ports
        wdf_cjson
)

# ---------------------------------------------------------------------------
# wdf_storage_program_json — 方案资产的 JSON 加载适配器
#
# 与 wdf_storage_json 分开：它反序列化方案模型，必须与 wdf_program_engine
# 一起链接；只需要参数/部署存储的最小接入不应被迫带上整个方案引擎。
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_storage_program_json
    SOURCES
        adapters/outbound/storage/json/engine_program_json.c
        adapters/outbound/storage/json/engine_program_manifest.c
    DEPENDS
        wdf_common
        wdf_ports
        wdf_cjson
        wdf_program_engine
)

# ---------------------------------------------------------------------------
# wdf_hal_sim — IO / 语音仿真后端
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_hal_sim
    SOURCES
        adapters/outbound/hal/sim/hal_io_sim.c
        adapters/outbound/hal/sim/hal_voice_sim.c
    DEPENDS
        wdf_common
        wdf_ports
)

# ---------------------------------------------------------------------------
# wdf_hal_engine_sim — 方案引擎 IO / 执行器仿真后端
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_hal_engine_sim
    SOURCES
        adapters/outbound/hal/sim/engine_io_sim.c
        adapters/outbound/hal/sim/engine_actuator_sim.c
    DEPENDS
        wdf_common
        wdf_domain
)

# ---------------------------------------------------------------------------
# wdf_hal_components — 与具体 vendor 无关的 HAL 组合件
# ---------------------------------------------------------------------------
_wdf_add_interface_lib(wdf_hal_components
    SOURCES
        adapters/outbound/hal/components/adc_gate/hal_adc_gate.c
        adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.c
        adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.c
    DEPENDS
        wdf_common
        wdf_ports
)
