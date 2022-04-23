cmake_minimum_required(VERSION 3.15)

project(FastCP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# cxxopts library
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/cxxopts)

# Basic cp
add_executable(cp ${CMAKE_CURRENT_SOURCE_DIR}/src/cp.cpp
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/buffer-lcm.cpp)

target_include_directories(cp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/)
target_link_libraries(cp cxxopts uring)

# fcp
add_executable(fcp ${CMAKE_CURRENT_SOURCE_DIR}/src/fcp2.cpp)
target_link_libraries(fcp uring)
target_include_directories(fcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/)

# release ops
add_compile_options(
    "$<$<CONFIG:RELEASE>:-O3 -march=native>"
)