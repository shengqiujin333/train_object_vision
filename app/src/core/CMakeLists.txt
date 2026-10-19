add_library(railhmi_core STATIC
    src/version.cpp
    src/video_diagnostics.cpp
    src/yaml.cpp
    src/config.cpp
    src/stream_state.cpp
    src/rules.cpp
    src/timebase.cpp
    src/frame_identity.cpp
    src/frame_layout.cpp
    src/compressed_ring.cpp
    src/recording_source.cpp
    src/persistence_model.cpp
    src/retention.cpp
    src/simulation.cpp
    src/navigation.cpp
    src/camera_registry.cpp
    src/layout.cpp
    src/grid.cpp
    src/channel_view.cpp
    src/status.cpp
    src/inference.cpp
    src/vision_model_capability.cpp
    src/vision_preprocess.cpp
    src/vision_detect_postprocess.cpp
    src/vision_seg_postprocess.cpp
    src/vision_two_stage.cpp
    src/alarm_center.cpp
    src/clip.cpp
    src/clip_request.cpp
    src/recording_provider.cpp
    src/replay.cpp
    src/replay_sync.cpp
    src/maintenance.cpp
    src/metrics.cpp
    src/permissions.cpp
    src/health.cpp
    src/recovery.cpp
    src/recovery_policy.cpp
    src/fault_catalog.cpp
)
target_include_directories(railhmi_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(railhmi_core PUBLIC cxx_std_17)

# 启动配置校验工具（与应用同一校验代码；无 Qt 依赖，可在最小工具链运行）
add_executable(railhmi-config-check tools/config_check.cpp)
target_link_libraries(railhmi-config-check PRIVATE railhmi_core)
set_target_properties(railhmi-config-check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 模拟输入/故障注入 CLI（ITEM-009；无 Qt/相机）
add_executable(railhmi-sim-inject tools/sim_inject.cpp)
target_link_libraries(railhmi-sim-inject PRIVATE railhmi_core)
set_target_properties(railhmi-sim-inject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 关键状态区/顶部条探针（ITEM-016；无 Qt/相机）
add_executable(railhmi-status-probe tools/status_probe.cpp)
target_link_libraries(railhmi-status-probe PRIVATE railhmi_core)
set_target_properties(railhmi-status-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 时间基准与回放同步探针（视频 ITEM-015；无 Qt/相机/GStreamer）
add_executable(railhmi-timebase-sync tools/timebase_sync_probe.cpp)
target_link_libraries(railhmi-timebase-sync PRIVATE railhmi_core)
set_target_properties(railhmi-timebase-sync PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 推理调度/队列/遥测探针（ITEM-017；无 Qt/RKNN）
add_executable(railhmi-ai-probe tools/ai_probe.cpp)
target_link_libraries(railhmi-ai-probe PRIVATE railhmi_core)
set_target_properties(railhmi-ai-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 视觉模型能力核验探针（视觉推理 ITEM-001；无 Qt/RKNN，夹具可自检）
add_executable(railhmi-model-probe tools/model_probe.cpp)
target_link_libraries(railhmi-model-probe PRIVATE railhmi_core)
set_target_properties(railhmi-model-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# YOLOv5 人形检测后处理探针（视觉推理 ITEM-003；纯逻辑夹具）
add_executable(railhmi-vision-detect tools/detect_probe.cpp)
target_link_libraries(railhmi-vision-detect PRIVATE railhmi_core)
set_target_properties(railhmi-vision-detect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# YOLOv8-seg 轨道分割后处理探针（视觉推理 ITEM-004；纯逻辑夹具）
add_executable(railhmi-vision-seg tools/seg_probe.cpp)
target_link_libraries(railhmi-vision-seg PRIVATE railhmi_core)
set_target_properties(railhmi-vision-seg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 同帧两阶段判定探针（视觉推理 ITEM-005；纯逻辑夹具）
add_executable(railhmi-vision-twostage tools/two_stage_probe.cpp)
target_link_libraries(railhmi-vision-twostage PRIVATE railhmi_core)
set_target_properties(railhmi-vision-twostage PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 报警中心模型探针（ITEM-018；无 Qt/SQLite）
add_executable(railhmi-alarm-probe tools/alarm_probe.cpp)
target_link_libraries(railhmi-alarm-probe PRIVATE railhmi_core)
set_target_properties(railhmi-alarm-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 保护片段/缺口探针（ITEM-019；含原子写盘，链 railhmi_platform）
add_executable(railhmi-clip-probe tools/clip_probe.cpp)
target_link_libraries(railhmi-clip-probe PRIVATE railhmi_core railhmi_platform)
set_target_properties(railhmi-clip-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 历史回放模型探针（ITEM-020；无 Qt/SQLite）
add_executable(railhmi-replay-probe tools/replay_probe.cpp)
target_link_libraries(railhmi-replay-probe PRIVATE railhmi_core)
set_target_properties(railhmi-replay-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 设备维护模型探针（ITEM-022；无 Qt/SQLite）
add_executable(railhmi-maintenance-probe tools/maintenance_probe.cpp)
target_link_libraries(railhmi-maintenance-probe PRIVATE railhmi_core)
set_target_properties(railhmi-maintenance-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 权限矩阵/二次确认探针（ITEM-023；无 Qt/SQLite）
add_executable(railhmi-permissions-probe tools/permissions_probe.cpp)
target_link_libraries(railhmi-permissions-probe PRIVATE railhmi_core)
set_target_properties(railhmi-permissions-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 健康/结构化日志/自恢复探针（ITEM-024；无 Qt/SQLite）
add_executable(railhmi-health-probe tools/health_probe.cpp)
target_link_libraries(railhmi-health-probe PRIVATE railhmi_core)
set_target_properties(railhmi-health-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 故障自检目录探针（ITEM-025；无 Qt）
add_executable(railhmi-fault-probe tools/fault_probe.cpp)
target_link_libraries(railhmi-fault-probe PRIVATE railhmi_core)
set_target_properties(railhmi-fault-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)
