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
- [ ] Build cleanly with VS2022 + WDK 26100.6584
- [ ] Test-sign package
- [ ] Install on Windows 11 x64
- [ ] Confirm device in Device Manager

## M2 - Synthetic video
- [x] Create capture pin descriptor
- [x] Advertise fixed 1280x720 @ ~30 fps intersection format
- [x] Add deterministic RGB32 synthetic frame generator
- [ ] Validate allocator/format negotiation in WDK build
- [ ] Verify Windows Camera application

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
