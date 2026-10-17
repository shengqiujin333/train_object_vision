# railhmi_media: target 专用媒体/设备适配层（GStreamer + librga）。
# 仅 target 档构建；preview 使用模拟输入，不依赖本层。

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

if(NOT RGA_INCLUDE_DIRS)
    find_path(RGA_INCLUDE_DIRS im2d.h HINTS /usr/include/rga /usr/local/include/rga)
endif()
if(NOT RGA_LIBRARIES)
    find_library(RGA_LIBRARIES rga HINTS /usr/lib/aarch64-linux-gnu /usr/local/lib)
endif()
if(NOT RGA_INCLUDE_DIRS OR NOT RGA_LIBRARIES)
    message(FATAL_ERROR "railhmi media: librga not found (need im2d.h + librga; install/locate RGA).")
endif()

add_library(railhmi_media STATIC
    src/pipeline_factory.cpp
    src/camera_stream.cpp
    src/multi_stream.cpp
    src/ai_worker.cpp
    src/video_capability.cpp
    src/frame_lease.cpp
    src/rga_convert.cpp
    src/camera_worker.cpp
    src/frame_dispatcher.cpp
    src/display_distributor.cpp
    src/display_registry.cpp
    src/ai_dispatcher.cpp
    src/recording_tap.cpp
    src/clip_writer.cpp
    src/media_provider.cpp
    src/recovery_controller.cpp
)
target_include_directories(railhmi_media PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(railhmi_media PRIVATE
    ${GST_INCLUDE_DIRS} ${RGA_INCLUDE_DIRS}
)
target_link_libraries(railhmi_media PUBLIC
    railhmi_core railhmi_platform
    ${GST_LIBRARIES} ${RGA_LIBRARIES}
)
target_compile_options(railhmi_media PRIVATE ${GST_CFLAGS_OTHER})

# 单路取流/状态机探针（ITEM-013；原生 GStreamer，支持 test/file/rtsp 源）
add_executable(railhmi-stream-probe tools/stream_probe.cpp)
target_link_libraries(railhmi-stream-probe PRIVATE railhmi_media)
set_target_properties(railhmi-stream-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 六路并发取流探针（ITEM-015；同时启动多路并注入单路断流）
add_executable(railhmi-multi-probe tools/multi_probe.cpp)
target_link_libraries(railhmi-multi-probe PRIVATE railhmi_media)
set_target_properties(railhmi-multi-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 推理工作线程运行器（ITEM-017；有界队列/降频/遥测 + 停止后 AI 不可用）
add_executable(railhmi-ai-runner tools/ai_runner.cpp)
target_link_libraries(railhmi-ai-runner PRIVATE railhmi_media)
set_target_properties(railhmi-ai-runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 视频源能力探测与配置绑定（视频 ITEM-001；元素/库版本/凭据/协商 caps）
add_executable(railhmi-video-capability tools/video_capability_tool.cpp)
target_link_libraries(railhmi-video-capability PRIVATE railhmi_media)
set_target_properties(railhmi-video-capability PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 不可变帧租约与缓冲区所有权探针（视频 ITEM-004）
add_executable(railhmi-frame-lease tools/frame_lease_probe.cpp)
target_link_libraries(railhmi-frame-lease PRIVATE railhmi_media)
set_target_properties(railhmi-frame-lease PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# NV12→BGR 格式转换探针（视频 ITEM-005）
add_executable(railhmi-rga-convert tools/rga_convert_probe.cpp)
target_link_libraries(railhmi-rga-convert PRIVATE railhmi_media)
set_target_properties(railhmi-rga-convert PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 每路取流 worker 与状态机探针（视频 ITEM-006）
add_executable(railhmi-camera-worker tools/camera_worker_probe.cpp)
target_link_libraries(railhmi-camera-worker PRIVATE railhmi_media)
set_target_properties(railhmi-camera-worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 三类消费者有界队列与背压探针（视频 ITEM-008）
add_executable(railhmi-backpressure tools/backpressure_probe.cpp)
target_link_libraries(railhmi-backpressure PRIVATE railhmi_media)
set_target_properties(railhmi-backpressure PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 显示分发与纹理交接探针（视频 ITEM-009）
add_executable(railhmi-display tools/display_probe.cpp)
target_link_libraries(railhmi-display PRIVATE railhmi_media)
set_target_properties(railhmi-display PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# AI 帧派发边界探针（视频 ITEM-010）
add_executable(railhmi-ai-dispatch tools/ai_dispatch_probe.cpp)
target_link_libraries(railhmi-ai-dispatch PRIVATE railhmi_media)
set_target_properties(railhmi-ai-dispatch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 录像压缩分支与滚动缓冲探针（视频 ITEM-011）
add_executable(railhmi-recording-tap tools/recording_tap_probe.cpp)
target_include_directories(railhmi-recording-tap PRIVATE ${GST_INCLUDE_DIRS})
target_compile_options(railhmi-recording-tap PRIVATE ${GST_CFLAGS_OTHER})
target_link_libraries(railhmi-recording-tap PRIVATE railhmi_media)
set_target_properties(railhmi-recording-tap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 保护片段固化与缺口探针（视频 ITEM-012）
add_executable(railhmi-clip-solidify tools/clip_solidify_probe.cpp)
target_include_directories(railhmi-clip-solidify PRIVATE ${GST_INCLUDE_DIRS})
target_compile_options(railhmi-clip-solidify PRIVATE ${GST_CFLAGS_OTHER})
target_link_libraries(railhmi-clip-solidify PRIVATE railhmi_media)
set_target_properties(railhmi-clip-solidify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 录像 provider 与回放媒体链路探针（视频 ITEM-013）
add_executable(railhmi-media-provider tools/media_provider_probe.cpp)
target_include_directories(railhmi-media-provider PRIVATE ${GST_INCLUDE_DIRS})
target_compile_options(railhmi-media-provider PRIVATE ${GST_CFLAGS_OTHER})
target_link_libraries(railhmi-media-provider PRIVATE railhmi_media)
set_target_properties(railhmi-media-provider PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

# 异常恢复控制器探针（视频 ITEM-014）
add_executable(railhmi-recovery tools/recovery_probe.cpp)
target_include_directories(railhmi-recovery PRIVATE ${GST_INCLUDE_DIRS})
target_compile_options(railhmi-recovery PRIVATE ${GST_CFLAGS_OTHER})
target_link_libraries(railhmi-recovery PRIVATE railhmi_media)
set_target_properties(railhmi-recovery PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)
