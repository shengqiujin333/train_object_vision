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

# 构建期由 docs/hmi/design_tokens.yaml 生成 tokens.json/Theme.qml/Tokens.qml/qmldir（ITEM-010）。
# 需 Python3 + PyYAML；缺失时跳过并保留已生成物（不静默改设计）。
find_package(Python3 COMPONENTS Interpreter QUIET)
set(RAILHMI_TOKENS_YAML "${CMAKE_SOURCE_DIR}/../docs/hmi/design_tokens.yaml")
if(Python3_Interpreter_FOUND AND EXISTS "${RAILHMI_TOKENS_YAML}")
    add_custom_target(railhmi_tokens ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_tokens.py
                --yaml ${RAILHMI_TOKENS_YAML}
                --json ${CMAKE_SOURCE_DIR}/resources/tokens.json
                --qml-dir ${CMAKE_CURRENT_SOURCE_DIR}/qml
        DEPENDS ${RAILHMI_TOKENS_YAML} ${CMAKE_SOURCE_DIR}/tools/gen_tokens.py
        COMMENT "Generating theme tokens from docs/hmi/design_tokens.yaml")
else()
    message(STATUS "railhmi ui: Python3/PyYAML or design_tokens.yaml not found -> using checked-in generated tokens")
endif()

# 版本信息注入 UI 上下文
add_executable(railhmi
    src/main.cpp
    src/alarm_model.cpp
    src/alarm_model.h
    src/replay_model.cpp
    src/replay_model.h
    src/maintenance_model.cpp
    src/maintenance_model.h
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
if(TARGET railhmi_tokens)
    add_dependencies(railhmi railhmi_tokens)
endif()
