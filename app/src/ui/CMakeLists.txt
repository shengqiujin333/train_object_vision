# railhmi Qt/QML 应用（target 或 preview）。
# 依赖 Qt5 Quick/Qml；缺失时以明确信息终止（不静默）。

find_package(Qt5 5.12 COMPONENTS Core Gui Quick Qml QUIET)
if(NOT Qt5_FOUND)
    message(FATAL_ERROR
        "railhmi ui: Qt5 Quick/Qml development packages not found. "
        "Install qtbase5-dev qtdeclarative5-dev qml-module-qtquick2 qml-module-qtquick-layouts "
        "qtbase5-dev-tools on the target (or matching host Qt for preview), "
        "or build the CoreTests mode which does not require Qt.")
endif()

set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)

# 版本信息注入 UI 上下文
add_executable(railhmi
    src/main.cpp
    qml.qrc
)
target_include_directories(railhmi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(railhmi PRIVATE RAILHMI_CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
target_link_libraries(railhmi PRIVATE
    Qt5::Core Qt5::Gui Qt5::Quick Qt5::Qml
    railhmi_core railhmi_platform
)
if(RAILHMI_BUILD_TARGET)
    target_link_libraries(railhmi PRIVATE railhmi_media)
endif()
