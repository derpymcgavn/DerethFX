cmake_minimum_required(VERSION 3.20)

project(DerethFX LANGUAGES CXX)

add_library(d3d9 SHARED
    src/d3d9_proxy.cpp
    src/d3d9_proxy.def
)

target_compile_features(d3d9 PRIVATE cxx_std_17)
target_compile_definitions(d3d9 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(d3d9 PRIVATE dxguid)

set_target_properties(d3d9 PROPERTIES
    OUTPUT_NAME d3d9
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)