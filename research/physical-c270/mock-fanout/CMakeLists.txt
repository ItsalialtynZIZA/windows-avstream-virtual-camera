cmake_minimum_required(VERSION 3.20)
project(AvCamMockFanout LANGUAGES CXX)
add_executable(AvCamMockFanout ../AvCamMockFanout.cpp)
target_compile_features(AvCamMockFanout PRIVATE cxx_std_17)

add_executable(AvCamMockIpcFanout ../AvCamMockIpcFanout.cpp)
target_compile_features(AvCamMockIpcFanout PRIVATE cxx_std_17)

add_executable(AvCamMockFrameIpc ../AvCamMockFrameIpc.cpp ../../../feeder/src/ObsSharedMemorySource.cpp)
target_include_directories(AvCamMockFrameIpc PRIVATE ../../../shared)
target_compile_features(AvCamMockFrameIpc PRIVATE cxx_std_17)

add_executable(AvCamPhysicalEndpointAcceptanceTest ../PhysicalEndpointAcceptanceTest.cpp)
target_compile_features(AvCamPhysicalEndpointAcceptanceTest PRIVATE cxx_std_17)
