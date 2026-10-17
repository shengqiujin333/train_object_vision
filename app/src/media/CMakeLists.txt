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
