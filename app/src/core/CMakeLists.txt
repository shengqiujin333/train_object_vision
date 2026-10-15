add_library(railhmi_core STATIC
    src/version.cpp
    src/yaml.cpp
    src/config.cpp
    src/stream_state.cpp
    src/rules.cpp
    src/timebase.cpp
    src/persistence_model.cpp
    src/retention.cpp
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
