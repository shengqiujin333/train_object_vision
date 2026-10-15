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
