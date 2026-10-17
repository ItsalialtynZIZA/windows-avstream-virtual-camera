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

add_executable(AvCamUvcControlTests uvc-controls-tests.cpp)
target_compile_features(AvCamUvcControlTests PRIVATE cxx_std_17)
target_include_directories(AvCamUvcControlTests PRIVATE ../shared)
if(MSVC)
    target_compile_options(AvCamUvcControlTests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME uvc-controls-tests COMMAND AvCamUvcControlTests)
