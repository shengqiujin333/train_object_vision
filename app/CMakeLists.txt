# railhmi — RK3588 车载监控 HMI 桌面应用（分层工程骨架，ITEM-003）
#
# 三档构建开关（由 build.sh 传入）:
#   RAILHMI_BUILD_TARGET     : aarch64 板端应用（Qt + GStreamer + librga + SQLite）
#   RAILHMI_BUILD_PREVIEW    : x86_64 主机预览（Qt + 模拟输入，无媒体/NPU）
#   RAILHMI_BUILD_CORE_TESTS : 仅 core+platform 单元测试（不依赖 Qt/GStreamer/相机）
#
# 分层:
#   railhmi_core     : 纯业务逻辑，仅 C++ 标准库（无 Qt/GStreamer/相机/SQLite）
#   railhmi_platform : 基础设施（日志/文件存储 + 可选 SQLite 持久化）
#   railhmi_media    : target 专用媒体/设备适配（GStreamer + librga + rknn）
#   railhmi          : Qt/QML 应用（target 或 preview）
#
# 主机编译不代表目标板运行；无工具链时由 build.sh 给出明确缺依赖提示，不静默失败。

cmake_minimum_required(VERSION 3.16)
project(railhmi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RAILHMI_BUILD_TARGET "Build aarch64 target application (Qt/GStreamer/RGA)" OFF)
option(RAILHMI_BUILD_PREVIEW "Build x86_64 host preview (Qt + simulation)" OFF)
option(RAILHMI_BUILD_CORE_TESTS "Build railhmi-core unit tests (stdlib only)" OFF)
option(RAILHMI_BUILD_MEDIA "Build railhmi_media only (target layer compile check; no Qt)" OFF)
option(RAILHMI_SIMULATION "Enable simulated inputs (preview / no camera)" OFF)

if(NOT RAILHMI_BUILD_TARGET AND NOT RAILHMI_BUILD_PREVIEW AND NOT RAILHMI_BUILD_CORE_TESTS AND NOT RAILHMI_BUILD_MEDIA)
    message(FATAL_ERROR
        "railhmi: no build mode selected. Invoke via app/build.sh {Release|Preview|CoreTests|MediaCheck} "
        "or pass -DRAILHMI_BUILD_TARGET=ON / -DRAILHMI_BUILD_PREVIEW=ON / -DRAILHMI_BUILD_CORE_TESTS=ON / -DRAILHMI_BUILD_MEDIA=ON.")
endif()

if(RAILHMI_BUILD_PREVIEW)
    set(RAILHMI_BUILD_MODE_STR "Preview")
elseif(RAILHMI_BUILD_TARGET)
    set(RAILHMI_BUILD_MODE_STR "Target")
elseif(RAILHMI_BUILD_MEDIA)
    set(RAILHMI_BUILD_MODE_STR "MediaCheck")
else()
    set(RAILHMI_BUILD_MODE_STR "CoreTests")
endif()

if(RAILHMI_SIMULATION OR RAILHMI_BUILD_PREVIEW)
    add_compile_definitions(RAILHMI_SIMULATION=1)
endif()
add_compile_definitions(RAILHMI_BUILD_MODE_STR="${RAILHMI_BUILD_MODE_STR}")

message(STATUS "railhmi: build mode = ${RAILHMI_BUILD_MODE_STR}")

# core + platform 在所有档位都构建（core 是最大可验证业务面）
add_subdirectory(src/core)
add_subdirectory(src/platform)

if(RAILHMI_BUILD_CORE_TESTS)
    enable_testing()
    add_subdirectory(tests/core)
endif()

# media 为 target 专用（或独立层验证）；ui 依赖 media（仅 target）
if(RAILHMI_BUILD_TARGET OR RAILHMI_BUILD_MEDIA)
    add_subdirectory(src/media)
endif()
if(RAILHMI_BUILD_TARGET OR RAILHMI_BUILD_PREVIEW)
    add_subdirectory(src/ui)
endif()
