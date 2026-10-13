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
- [x] SetupAPI discovery in feeder
- [x] Open driver-facing device interface from feeder
- [ ] Register producer device interface from driver
- [ ] Implement frame submission IOCTL/property path
- [ ] Handle producer disconnect/reconnect
- [ ] Add bounded queue and frame dropping

## M4 - OBS bridge
- [ ] Enumerate OBS Virtual Camera
- [ ] Capture frames with Media Foundation
- [ ] Convert to driver input format
- [ ] Feed frames continuously

## M5 - Hardening
- [ ] Concurrency review
- [ ] Pool/memory validation
- [ ] IRQL review
- [ ] Driver Verifier
- [ ] Static analysis
- [ ] Installer and signing documentation
