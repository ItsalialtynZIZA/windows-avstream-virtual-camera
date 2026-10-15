cmake_minimum_required(VERSION 3.24)
project(AvCamBusCtl LANGUAGES CXX)

add_executable(AvCamBusCtl AvCamBusCtl.cpp)
target_compile_features(AvCamBusCtl PRIVATE cxx_std_17)
target_include_directories(AvCamBusCtl PRIVATE ../shared)
target_compile_definitions(AvCamBusCtl PRIVATE UNICODE _UNICODE)
target_link_libraries(AvCamBusCtl PRIVATE setupapi)

if(MSVC)
    target_compile_options(AvCamBusCtl PRIVATE /W4 /WX /permissive-)
endif()
