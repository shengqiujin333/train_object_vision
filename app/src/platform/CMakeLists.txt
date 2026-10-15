add_library(railhmi_platform STATIC
    src/log.cpp
    src/file_store.cpp
)
target_include_directories(railhmi_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(railhmi_platform PUBLIC railhmi_core)

# SQLite 持久化（ITEM-007）。优先使用 libsqlite3-dev；若只有运行时库 libsqlite3.so.0
# （无 sqlite3.h / .so 符号链接），则链接运行时库并使用项目自带最小头 shim，无需安装开发包。
set(RAILHMI_HAVE_SQLITE FALSE)
set(RAILHMI_SQLITE_LIBS "")
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    set(RAILHMI_HAVE_SQLITE TRUE)
    set(RAILHMI_SQLITE_LIBS SQLite::SQLite3)
    message(STATUS "railhmi: SQLite3 ${SQLite3_VERSION} (dev package) -> persistence enabled")
else()
    find_file(RAILHMI_SQLITE3_RUNTIME NAMES libsqlite3.so.0 libsqlite3.so
              PATHS /lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
                    /lib /usr/lib /usr/local/lib)
    if(RAILHMI_SQLITE3_RUNTIME)
        set(RAILHMI_HAVE_SQLITE TRUE)
        set(RAILHMI_SQLITE_LIBS ${RAILHMI_SQLITE3_RUNTIME})
        message(STATUS "railhmi: SQLite runtime lib ${RAILHMI_SQLITE3_RUNTIME} + shim header -> persistence enabled")
    endif()
endif()

if(RAILHMI_HAVE_SQLITE)
    target_sources(railhmi_platform PRIVATE src/sqlite_store.cpp src/repository.cpp)
    target_compile_definitions(railhmi_platform PUBLIC RAILHMI_HAVE_SQLITE=1)
    target_include_directories(railhmi_platform PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite)
    target_link_libraries(railhmi_platform PUBLIC ${RAILHMI_SQLITE_LIBS})
else()
    message(STATUS "railhmi: SQLite not found -> persistence disabled "
                   "(install libsqlite3-dev, or provide libsqlite3.so.0 for the shim path)")
endif()
