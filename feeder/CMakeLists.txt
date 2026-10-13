cmake_minimum_required(VERSION 3.24)
project(AvCamFeeder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AvCamFeeder
    src/main.cpp
    src/TestPatternFrameSource.cpp
)

target_include_directories(AvCamFeeder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(AvCamFeeder PRIVATE
    setupapi
)
