add_library(railhmi_platform STATIC
    src/log.cpp
    src/file_store.cpp
)
target_include_directories(railhmi_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(railhmi_platform PUBLIC railhmi_core)

# SQLite 为可选持久化组件（item-007/008）。开发包缺失时明确提示并跳过，不静默失败。
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_sources(railhmi_platform PRIVATE src/sqlite_store.cpp)
    target_compile_definitions(railhmi_platform PUBLIC RAILHMI_HAVE_SQLITE=1)
    target_link_libraries(railhmi_platform PUBLIC SQLite::SQLite3)
    message(STATUS "railhmi: SQLite3 ${SQLite3_VERSION} found -> SqliteStore enabled")
else()
    message(STATUS "railhmi: SQLite3 dev not found -> SqliteStore disabled (install libsqlite3-dev to enable)")
endif()
