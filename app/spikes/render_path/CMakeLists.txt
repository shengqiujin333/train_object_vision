# railhmi 渲染路径选型 spike (ITEM-002)
#
# 目标板 (RK3588, Ubuntu 22.04 aarch64) 本机构建:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
#
# 依赖: Qt5(Quick/Qml/Gui/OpenGL) libs  gstreamer-1.0 + app + video  librga  pkg-config
# 说明: 无交叉编译; 主机若缺 Qt/GStreamer/librga 则本 spike 不能构建(属环境缺口)。

cmake_minimum_required(VERSION 3.16)
project(railhmi_render_spike CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Quick Qml)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

# librga: 优先 pkg-config, 回退到 find_library
pkg_check_modules(RGA librga)
if(NOT RGA_FOUND)
    find_path(RGA_INCLUDE_DIRS im2d.h HINTS /usr/include/rga /usr/local/include/rga)
    find_library(RGA_LIBRARIES rga HINTS /usr/lib/aarch64-linux-gnu /usr/local/lib)
    if(NOT RGA_INCLUDE_DIRS OR NOT RGA_LIBRARIES)
        message(FATAL_ERROR "librga not found (need im2d.h + librga). Install/locate RGA first.")
    endif()
endif()

add_executable(render_spike main.cpp qml.qrc)

target_include_directories(render_spike PRIVATE
    ${GST_INCLUDE_DIRS} ${RGA_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(render_spike PRIVATE
    Qt5::Core Qt5::Gui Qt5::Quick Qt5::Qml
    ${GST_LIBRARIES} ${RGA_LIBRARIES})

target_compile_options(render_spike PRIVATE ${GST_CFLAGS_OTHER})
