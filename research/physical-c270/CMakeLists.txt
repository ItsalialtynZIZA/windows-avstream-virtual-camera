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
# Never enable device-wide sample replacement in the default shipped build.
# A separate opt-in lab build must be packaged and installed by its dedicated,
# reversible two-camera tool. It affects ALL consumers of the physical C270.
option(AVCAM_DUAL_CAMERA_LAB "Build experimental device-wide C270 output for a separate internal capture camera" OFF)
if(AVCAM_DUAL_CAMERA_LAB)
  target_compile_definitions(AvCamPhysicalDeviceMft PRIVATE AVCAM_DUAL_CAMERA_LAB_MFT)
  set_target_properties(AvCamPhysicalDeviceMft PROPERTIES OUTPUT_NAME "AvCamDualCameraDeviceMft")
endif()
# The physical C270 is now shared by Deep Live Cam and Chrome using
# Windows 11 25H2 Multi-app camera mode. A Device MFT is device-scoped;
# unscoped OBS replacement risks processing Deep Live Cam's input and looping.
# Refuse accidental enablement even when a builder passes -D or /D directly.
get_target_property(avcam_dmft_defs AvCamPhysicalDeviceMft COMPILE_DEFINITIONS)
if(avcam_dmft_defs)
  string(JOIN ";" avcam_dmft_defs_joined ${avcam_dmft_defs})
  if(avcam_dmft_defs_joined MATCHES "AVCAM_EXPERIMENTAL_UNSCOPED_MFT_REPLACEMENT")
    message(FATAL_ERROR "Unscoped Device MFT replacement is forbidden in shipped builds")
  endif()
endif()

target_compile_definitions(AvCamPhysicalDeviceMft PRIVATE
  UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00
  SECURITY_WIN32 MFT_UNIQUE_METHOD_NAMES MF_DEVICEMFT_ALLOW_MFT0_LOAD)
target_link_libraries(AvCamPhysicalDeviceMft PRIVATE
  onecore d2d1 mfcore mfplat mfuuid uuid ole32 oleaut32
  mfreadwrite windowscodecs shlwapi advapi32)
if(MSVC)
  target_compile_options(AvCamPhysicalBridge PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(AvCamPhysicalDeviceMft PRIVATE /W3 /utf-8)
  set_target_properties(AvCamPhysicalBridge PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_target_properties(AvCamPhysicalDeviceMft PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    LINK_FLAGS "/DEF:${sample}/Source.def")
endif()
