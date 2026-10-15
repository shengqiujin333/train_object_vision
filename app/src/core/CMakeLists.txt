add_library(railhmi_core STATIC
    src/version.cpp
)
target_include_directories(railhmi_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(railhmi_core PUBLIC cxx_std_17)
