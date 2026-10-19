cmake_minimum_required(VERSION 3.20)
project(AvCamUdeContractTests LANGUAGES CXX)
enable_testing()
add_executable(AvCamUdeContractTests uvc-protocol-tests.cpp)
target_compile_features(AvCamUdeContractTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeContractTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeContractTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-uvc-protocol COMMAND AvCamUdeContractTests)

add_executable(AvCamUdeIsoTests iso-packet-tests.cpp)
target_compile_features(AvCamUdeIsoTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeIsoTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeIsoTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-iso-packet-bounds COMMAND AvCamUdeIsoTests)


add_executable(AvCamUdeIsoUrbLayoutTests iso-urb-layout-tests.cpp)
target_compile_features(AvCamUdeIsoUrbLayoutTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeIsoUrbLayoutTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeIsoUrbLayoutTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-iso-urb-preflight COMMAND AvCamUdeIsoUrbLayoutTests)


add_executable(AvCamUdeRequestLifecycleTests request-lifecycle-tests.cpp)
target_compile_features(AvCamUdeRequestLifecycleTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeRequestLifecycleTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeRequestLifecycleTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-request-lifecycle COMMAND AvCamUdeRequestLifecycleTests)

add_executable(AvCamUdeFrameSourceTests frame-source-tests.cpp)
target_compile_features(AvCamUdeFrameSourceTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeFrameSourceTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeFrameSourceTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-frame-source-leases COMMAND AvCamUdeFrameSourceTests)

add_executable(AvCamUdeEndpointDrainTests endpoint-drain-tests.cpp)
target_compile_features(AvCamUdeEndpointDrainTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeEndpointDrainTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeEndpointDrainTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-endpoint-drain COMMAND AvCamUdeEndpointDrainTests)

add_executable(AvCamUdeFramePumpTests frame-pump-tests.cpp)
target_compile_features(AvCamUdeFramePumpTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeFramePumpTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeFramePumpTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-mjpeg-frame-pump COMMAND AvCamUdeFramePumpTests)

add_executable(AvCamUdeUploadProtocolTests upload-protocol-tests.cpp)
target_compile_features(AvCamUdeUploadProtocolTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeUploadProtocolTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeUploadProtocolTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-mjpeg-upload-protocol COMMAND AvCamUdeUploadProtocolTests)

# A research-only Windows user-mode feeder. CI compiles it but does not run,
# install, sign, upload, or publish the binary.
if(WIN32)
    add_executable(AvCamUdeUploadTool ../tools/AvCamUdeUpload.cpp)
    target_compile_features(AvCamUdeUploadTool PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(AvCamUdeUploadTool PRIVATE /W4 /WX)
    endif()
endif()

# Real Windows Imaging Component acceptance, unlike the synthetic JPEG
# marker-only protocol fixtures. CI must decode the reconstructed ISO image.
if(WIN32)
    add_executable(AvCamUdeWicJpegTests wic-real-jpeg-tests.cpp)
    target_compile_features(AvCamUdeWicJpegTests PRIVATE cxx_std_17)
    target_link_libraries(AvCamUdeWicJpegTests PRIVATE windowscodecs ole32)
    if(MSVC)
        target_compile_options(AvCamUdeWicJpegTests PRIVATE /W4 /WX)
    endif()
    add_test(NAME udecx-real-wic-jpeg-iso-decode COMMAND AvCamUdeWicJpegTests)
endif()

# OBS/Media Foundation capture -> WIC 1920x1080 MJPEG user-mode pipeline.
# Built and test-encoded in Actions; no host controller is installed.
if(WIN32)
    add_executable(AvCamUdeObsFeeder
        ../tools/AvCamUdeObsFeeder.cpp
        ../tools/AvCamUdeMjpegEncoder.cpp
        ../../../feeder/src/MediaFoundationFrameSource.cpp
    )
    target_include_directories(AvCamUdeObsFeeder PRIVATE ../../../shared)
    target_compile_features(AvCamUdeObsFeeder PRIVATE cxx_std_17)
    target_compile_definitions(AvCamUdeObsFeeder PRIVATE
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(AvCamUdeObsFeeder PRIVATE
        windowscodecs ole32 mf mfplat mfreadwrite mfuuid)
    if(MSVC)
        target_compile_options(AvCamUdeObsFeeder PRIVATE /W4 /WX /permissive-)
    endif()

    add_executable(AvCamUdeBgraJpegTests
        mf-bgra-mjpeg-encode-tests.cpp
        ../tools/AvCamUdeMjpegEncoder.cpp)
    target_compile_features(AvCamUdeBgraJpegTests PRIVATE cxx_std_17)
    target_link_libraries(AvCamUdeBgraJpegTests PRIVATE windowscodecs ole32)
    if(MSVC)
        target_compile_options(AvCamUdeBgraJpegTests PRIVATE /W4 /WX)
    endif()
    add_test(NAME udecx-mf-bgra-to-decodable-jpeg
        COMMAND AvCamUdeBgraJpegTests)
endif()

# First real Media Foundation sample acceptance binary (compile-only in CI).
# Execute only in a disposable Windows VM after explicit, separate setup.
if(WIN32)
    add_executable(AvCamUdeMfProbe
        ../tools/AvCamUdeMfProbe.cpp)
    target_compile_features(AvCamUdeMfProbe PRIVATE cxx_std_17)
    target_link_libraries(AvCamUdeMfProbe PRIVATE
        mf mfplat mfreadwrite mfuuid ole32)
    if(MSVC)
        target_compile_options(AvCamUdeMfProbe PRIVATE /W4 /WX)
    endif()
endif()

# Portable model for bounded asynchronous ISO request admission and cancel
# races. This does NOT enable live EP 0x81 completions.
add_executable(AvCamUdeAsyncIsoSchedulerTests async-iso-scheduler-tests.cpp)
target_compile_features(AvCamUdeAsyncIsoSchedulerTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeAsyncIsoSchedulerTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeAsyncIsoSchedulerTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-async-iso-cancel-purge-reset
         COMMAND AvCamUdeAsyncIsoSchedulerTests)

# Validate that UdeCx ISO completion uses the SUM of packet lengths, while
# the untouched host packet offsets still bound every sparse payload.
add_executable(AvCamUdeIsoCompletionTests iso-completion-tests.cpp)
target_compile_features(AvCamUdeIsoCompletionTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeIsoCompletionTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeIsoCompletionTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-sparse-iso-completion-metadata
         COMMAND AvCamUdeIsoCompletionTests)

# The host's explicit StartFrame is 1 ms, not one high-speed microframe;
# reject late/backward URBs and validate ASAP/wrap-aware scheduling.
add_executable(AvCamUdeIsoClockTests iso-clock-tests.cpp)
target_compile_features(AvCamUdeIsoClockTests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(AvCamUdeIsoClockTests PRIVATE /W4 /WX)
else()
    target_compile_options(AvCamUdeIsoClockTests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME udecx-usb-iso-startframe-clock COMMAND AvCamUdeIsoClockTests)
