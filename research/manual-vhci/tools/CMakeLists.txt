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

# Root-enumerated PnP device creator; replaces DevCon for fresh test machines.
add_executable(AvCamBusInstall AvCamBusInstall.cpp)
target_compile_features(AvCamBusInstall PRIVATE cxx_std_17)
target_compile_definitions(AvCamBusInstall PRIVATE UNICODE _UNICODE)
target_link_libraries(AvCamBusInstall PRIVATE setupapi newdev)

if(MSVC)
    target_compile_options(AvCamBusInstall PRIVATE /W4 /WX /permissive-)
endif()

# User-mode-only Media Foundation capture diagnosis. No custom driver install.
add_executable(AvCamMfProbe AvCamMfProbe.cpp)
target_compile_features(AvCamMfProbe PRIVATE cxx_std_17)
target_compile_definitions(AvCamMfProbe PRIVATE UNICODE _UNICODE)
target_link_libraries(AvCamMfProbe PRIVATE mfplat mf mfreadwrite mfuuid ole32)
if(MSVC)
    target_compile_options(AvCamMfProbe PRIVATE /W4 /WX /permissive-)
endif()
