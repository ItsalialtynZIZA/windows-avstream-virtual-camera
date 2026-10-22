cmake_minimum_required(VERSION 3.20)
project(AvCamMockFanout LANGUAGES CXX)

add_executable(AvCamRouterRunPolicyTest ../AvCamRouterRunPolicyTest.cpp)
target_compile_features(AvCamRouterRunPolicyTest PRIVATE cxx_std_17)

add_executable(AvCamRouterDiagnosticsTest ../AvCamRouterDiagnosticsTest.cpp)
target_compile_features(AvCamRouterDiagnosticsTest PRIVATE cxx_std_17)

# This is the separate Win11 software-camera API controller.
# Never auto-register a camera or a COM source in CI.
# Unregistered, camera-independent native Media Foundation COM server.
# The first streaming source is IPC V2 OBS ONLY. Never auto-install or
# use a third-party/physical Logitech CLSID on the Windows runner.
add_library(AvCamMfSource SHARED ../mf-source/AvCamMfSource.cpp)
target_compile_features(AvCamMfSource PRIVATE cxx_std_17)
target_link_libraries(AvCamMfSource PRIVATE mfplat mfuuid ole32)
set_target_properties(AvCamMfSource PROPERTIES PREFIX "")

add_executable(AvCamMfColorTest ../mf-source/AvCamMfColorTest.cpp)
target_compile_features(AvCamMfColorTest PRIVATE cxx_std_17)

add_executable(AvCamSoftwareCameraControl ../AvCamSoftwareCameraControl.cpp)
target_compile_features(AvCamSoftwareCameraControl PRIVATE cxx_std_17)
target_compile_definitions(AvCamSoftwareCameraControl PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(AvCamSoftwareCameraControl PRIVATE mfplat ole32 advapi32)

add_executable(AvCamRouterIpcProbe ../AvCamRouterIpcProbe.cpp)
target_compile_features(AvCamRouterIpcProbe PRIVATE cxx_std_17)

add_executable(AvCamRouterIpcProcessTest ../AvCamRouterIpcProcessTest.cpp)
target_compile_features(AvCamRouterIpcProcessTest PRIVATE cxx_std_17)

add_executable(AvCamVirtualReadinessTest ../AvCamVirtualReadinessTest.cpp)
target_compile_features(AvCamVirtualReadinessTest PRIVATE cxx_std_17)

add_executable(AvCamFeederIpcSourceTest
  ../AvCamFeederIpcSourceTest.cpp
  ../../../feeder/src/AvCamIpcFrameSource.cpp)
target_compile_features(AvCamFeederIpcSourceTest PRIVATE cxx_std_17)

add_executable(AvCamAsyncObsWorkerTest ../AvCamAsyncObsWorkerTest.cpp)
target_compile_features(AvCamAsyncObsWorkerTest PRIVATE cxx_std_17)

add_executable(AvCamPreviewSelectionTest ../AvCamPreviewSelectionTest.cpp)
target_compile_features(AvCamPreviewSelectionTest PRIVATE cxx_std_17)

add_executable(AvCamRouterPreview WIN32 ../AvCamRouterPreview.cpp)
target_compile_features(AvCamRouterPreview PRIVATE cxx_std_17)
target_compile_definitions(AvCamRouterPreview PRIVATE UNICODE _UNICODE)
target_link_libraries(AvCamRouterPreview PRIVATE user32 gdi32 shell32)

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

# Reuse the shipping AVStream feeder build, now with IPC V2 input modes.
# This does not install or register the test-signed virtual camera driver.
add_subdirectory(../../../feeder "${CMAKE_CURRENT_BINARY_DIR}/avcam-feeder")
