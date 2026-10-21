cmake_minimum_required(VERSION 3.20)
project(AvCamMockFanout LANGUAGES CXX)

add_executable(AvCamRouterRunPolicyTest ../AvCamRouterRunPolicyTest.cpp)
target_compile_features(AvCamRouterRunPolicyTest PRIVATE cxx_std_17)

add_executable(AvCamRouterIpcProbe ../AvCamRouterIpcProbe.cpp)
target_compile_features(AvCamRouterIpcProbe PRIVATE cxx_std_17)

add_executable(AvCamRouterPhysicalRawObs
  ../AvCamRouterPhysicalRawObs.cpp
  ../../../feeder/src/MediaFoundationFrameSource.cpp
  ../../../feeder/src/ObsSharedMemorySource.cpp)
target_include_directories(AvCamRouterPhysicalRawObs PRIVATE
  ../../../shared ../../../feeder/src)
target_compile_features(AvCamRouterPhysicalRawObs PRIVATE cxx_std_17)
target_link_libraries(AvCamRouterPhysicalRawObs PRIVATE
  mfplat mfreadwrite mf mfuuid ole32)

add_executable(AvCamRouterObsSmoke ../AvCamRouterObsSmoke.cpp ../../../feeder/src/ObsSharedMemorySource.cpp)
target_include_directories(AvCamRouterObsSmoke PRIVATE ../../../shared)
target_compile_features(AvCamRouterObsSmoke PRIVATE cxx_std_17)

add_executable(AvCamDualChannelRouterTest ../AvCamDualChannelRouterTest.cpp)
target_compile_features(AvCamDualChannelRouterTest PRIVATE cxx_std_17)

add_executable(AvCamMockFanout ../AvCamMockFanout.cpp)
target_compile_features(AvCamMockFanout PRIVATE cxx_std_17)

add_executable(AvCamMockIpcFanout ../AvCamMockIpcFanout.cpp)
target_compile_features(AvCamMockIpcFanout PRIVATE cxx_std_17)

add_executable(AvCamMockFrameIpc ../AvCamMockFrameIpc.cpp ../../../feeder/src/ObsSharedMemorySource.cpp)
target_include_directories(AvCamMockFrameIpc PRIVATE ../../../shared)
target_compile_features(AvCamMockFrameIpc PRIVATE cxx_std_17)

add_executable(AvCamPhysicalEndpointAcceptanceTest ../PhysicalEndpointAcceptanceTest.cpp)
target_compile_features(AvCamPhysicalEndpointAcceptanceTest PRIVATE cxx_std_17)

add_executable(AvCamEnumeratePhysicalC270 ../AvCamEnumeratePhysicalC270.cpp)
target_compile_features(AvCamEnumeratePhysicalC270 PRIVATE cxx_std_17)
target_link_libraries(AvCamEnumeratePhysicalC270 PRIVATE setupapi)
