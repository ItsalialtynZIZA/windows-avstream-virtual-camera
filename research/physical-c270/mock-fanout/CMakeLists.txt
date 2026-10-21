cmake_minimum_required(VERSION 3.20)
project(AvCamMockFanout LANGUAGES CXX)
add_executable(AvCamMockFanout ../AvCamMockFanout.cpp)
target_compile_features(AvCamMockFanout PRIVATE cxx_std_17)

add_executable(AvCamMockIpcFanout ../AvCamMockIpcFanout.cpp)
target_compile_features(AvCamMockIpcFanout PRIVATE cxx_std_17)
