add_executable(railhmi-core-tests
    main.cpp
)
target_link_libraries(railhmi-core-tests PRIVATE railhmi_core railhmi_platform)

# 固定输出到 <build>/tests/，与构建入口约定一致（build-core/tests/railhmi-core-tests）
set_target_properties(railhmi-core-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_test(NAME railhmi-core-tests COMMAND railhmi-core-tests)
