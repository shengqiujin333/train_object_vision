add_executable(railhmi-core-tests
    main.cpp
    test_util.cpp
    test_platform.cpp
    test_config.cpp
    test_stream.cpp
    test_rules.cpp
    test_persistence.cpp
    test_simulation.cpp
    test_navigation.cpp
    test_registry.cpp
    test_layout.cpp
    test_grid.cpp
    test_status.cpp
    test_inference.cpp
    test_vision_model_capability.cpp
    test_vision_preprocess.cpp
    test_vision_detect_postprocess.cpp
    test_vision_seg_postprocess.cpp
    test_frame_identity.cpp
    test_frame_layout.cpp
    test_bounded_queue.cpp
    test_compressed_ring.cpp
    test_recording_source.cpp
    test_clip_request.cpp
    test_recording_provider.cpp
    test_recovery_policy.cpp
    test_replay_sync.cpp
    test_video_diagnostics.cpp
    test_metrics.cpp
    test_alarm_center.cpp
    test_clip.cpp
    test_replay.cpp
    test_maintenance.cpp
    test_permissions.cpp
    test_health.cpp
)
target_link_libraries(railhmi-core-tests PRIVATE railhmi_core railhmi_platform)

# 基线配置目录（供测试加载真实 config/*.yaml；随工程上传/构建路径解析）
target_compile_definitions(railhmi-core-tests PRIVATE
    RAILHMI_CONFIG_DIR="${CMAKE_SOURCE_DIR}/config"
)

# 固定输出到 <build>/tests/，与构建入口约定一致（build-core/tests/railhmi-core-tests）
set_target_properties(railhmi-core-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_test(NAME railhmi-core-tests COMMAND railhmi-core-tests)
