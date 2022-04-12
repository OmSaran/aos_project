cmake_minimum_required(VERSION 3.15)

project(FastCP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(cp ${CMAKE_CURRENT_SOURCE_DIR}/src/cp.cpp
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/buffer-lcm.cpp)

target_include_directories(cp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/)

# cxxopts library
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/cxxopts)
target_link_libraries(cp cxxopts)
