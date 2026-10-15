cmake_minimum_required(VERSION 3.24)
project(AvCamManualVhciTests LANGUAGES CXX)

add_executable(AvCamDescriptorTests descriptor-tests.cpp)
target_compile_features(AvCamDescriptorTests PRIVATE cxx_std_17)
target_include_directories(AvCamDescriptorTests PRIVATE ../shared)

if(MSVC)
    target_compile_options(AvCamDescriptorTests PRIVATE /W4 /WX /permissive-)
endif()

enable_testing()
add_test(NAME descriptor-tests COMMAND AvCamDescriptorTests)
