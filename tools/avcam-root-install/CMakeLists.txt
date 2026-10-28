cmake_minimum_required(VERSION 3.24)
project(AvCamRootInstall LANGUAGES CXX)

# User-mode SetupAPI/NewDev utility. It installs only the distinct
# Root\AvCamVirtualCamera endpoint; no USB, OEM or Sonix operations.
add_executable(AvCamRootInstall AvCamRootInstall.cpp)
target_compile_features(AvCamRootInstall PRIVATE cxx_std_17)
target_compile_definitions(AvCamRootInstall PRIVATE UNICODE _UNICODE)
target_link_libraries(AvCamRootInstall PRIVATE setupapi newdev)

if(MSVC)
  target_compile_options(AvCamRootInstall PRIVATE /W4 /WX /permissive-)
endif()
