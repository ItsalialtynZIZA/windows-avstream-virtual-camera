cmake_minimum_required(VERSION 3.24)
project(AvCamFeeder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AvCamFeeder
    src/main.cpp
    src/TestPatternFrameSource.cpp
    src/MediaFoundationFrameSource.cpp
)

target_include_directories(AvCamFeeder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(AvCamFeeder PRIVATE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

target_link_libraries(AvCamFeeder PRIVATE
    setupapi
    mf
    mfplat
    mfreadwrite
    mfuuid
    ole32
)
