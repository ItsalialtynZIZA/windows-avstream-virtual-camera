cmake_minimum_required(VERSION 3.20)
project(AvCamPhysicalC270 LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "AvCam Physical C270 mode targets Windows only")
endif()
if(NOT DEFINED MICROSOFT_DMFT_DIR OR NOT EXISTS "${MICROSOFT_DMFT_DIR}/multipinmft.cpp")
  message(FATAL_ERROR "Specify -DMICROSOFT_DMFT_DIR=<adapted Microsoft sample folder>")
endif()
set(sample "${MICROSOFT_DMFT_DIR}")
set(base "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(AvCamPhysicalBridge
  AvCamPhysicalBridge.cpp
  ../udecx/tools/AvCamUdeMjpegEncoder.cpp
  ../../feeder/src/ObsSharedMemorySource.cpp)
target_include_directories(AvCamPhysicalBridge PRIVATE
  ../../shared ../../feeder/src)
target_compile_definitions(AvCamPhysicalBridge PRIVATE
  AVCAM_UDE_C270_COMPAT_PROFILE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_features(AvCamPhysicalBridge PRIVATE cxx_std_17)
target_link_libraries(AvCamPhysicalBridge PRIVATE windowscodecs ole32 advapi32)

set(dmft_sources
  multipinmft.cpp multipinmfthelpers.cpp multipinmftutils.cpp
  basepin.cpp custompin.cpp mftpeventgenerator.cpp
  dllmain.cpp stdafxsrc.cpp AvCamPhysicalHook.cpp)
set(dmft_paths)
foreach(item IN LISTS dmft_sources)
  list(APPEND dmft_paths "${sample}/${item}")
endforeach()
add_library(AvCamPhysicalDeviceMft SHARED ${dmft_paths})
target_include_directories(AvCamPhysicalDeviceMft PRIVATE
  "${sample}" "${base}" "${MICROSOFT_WIL_INCLUDE}")
target_compile_features(AvCamPhysicalDeviceMft PRIVATE cxx_std_17)
target_compile_definitions(AvCamPhysicalDeviceMft PRIVATE
  UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00
  SECURITY_WIN32 MFT_UNIQUE_METHOD_NAMES MF_DEVICEMFT_ALLOW_MFT0_LOAD)
target_link_libraries(AvCamPhysicalDeviceMft PRIVATE
  onecore d2d1 mfcore mfplat mfuuid uuid ole32 oleaut32
  mfreadwrite windowscodecs shlwapi)
if(MSVC)
  target_compile_options(AvCamPhysicalBridge PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(AvCamPhysicalDeviceMft PRIVATE /W3 /utf-8)
  set_target_properties(AvCamPhysicalBridge PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_target_properties(AvCamPhysicalDeviceMft PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    LINK_FLAGS "/DEF:${sample}/Source.def")
endif()
