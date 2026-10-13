# Roadmap

## M0 - Repository/bootstrap
- [x] Repository structure
- [x] Feeder CMake project
- [x] Shared frame protocol
- [x] AVStream driver entry point
- [x] Windows CI for feeder

## M1 - Camera enumeration
- [x] Replace placeholder with AVStream minidriver entry point
- [x] Register camera filter categories
- [x] Complete root-enumerated INF/service skeleton
- [x] Build cleanly with VS2022 + WDK 26100.6584
- [x] Test-sign package in CI
- [x] Build signed INF/CAT/SYS package artifact
- [ ] Install on Windows 11 x64
- [ ] Confirm device in Device Manager

## M2 - Synthetic video
- [x] Create capture pin descriptor
- [x] Advertise exact 1280x720 RGB32 @ ~30 fps KS_DATARANGE_VIDEO
- [x] Validate connection format through SetDataFormat
- [x] Add deterministic RGB32 synthetic frame generator
- [x] Validate allocator/format code in WDK build
- [x] Publish test package artifact from CI
- [ ] Verify Windows Camera application
- [ ] Verify Chrome/Edge camera enumeration

## M3 - User-mode frame transport
- [x] Shared frame protocol
- [x] Register/enable producer discovery device interface
- [x] SetupAPI producer discovery in feeder
- [x] Define private AVStream KS frame-transport method set
- [x] Validate fixed 1280x720 BGRA32 producer packets
- [x] Implement ref-counted nonpaged latest-frame store
- [x] Switch capture output to submitted frame with synthetic fallback
- [x] Probe camera filters and identify AvCam through the private KS method
- [x] Stream moving BGRA32 test frames continuously from feeder
- [x] Compile/package driver and feeder successfully in CI
- [ ] Runtime-test private method transport on Windows 11 x64
- [ ] Add automatic producer/filter reconnect after device restart
- [ ] Add frame/drop/fallback counters and diagnostics

## M4 - OBS bridge
- [ ] Introduce feeder frame-source abstraction
- [ ] Enumerate OBS Virtual Camera with Media Foundation
- [ ] Capture frames from OBS
- [ ] Convert/scale to 1280x720 BGRA32 when needed
- [ ] Feed frames continuously through the existing KS transport
- [ ] Handle OBS source disconnect/reconnect

## M5 - Hardening
- [ ] Concurrency review
- [ ] Pool/memory validation
- [ ] IRQL review
- [ ] Driver Verifier
- [ ] Static analysis
- [ ] Installer and signing documentation
