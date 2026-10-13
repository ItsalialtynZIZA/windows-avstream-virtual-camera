# Roadmap

## M0 - Repository/bootstrap
- [x] Repository structure
- [x] Feeder CMake project
- [x] Shared frame protocol
- [x] Driver placeholder
- [x] Windows CI for feeder

## M1 - Camera enumeration
- [ ] Replace placeholder with AVStream minidriver
- [ ] Register camera class/interface
- [ ] Complete INF
- [ ] Build with VS2022 + WDK
- [ ] Test-sign package
- [ ] Install on Windows 11 x64
- [ ] Confirm device in Device Manager

## M2 - Synthetic video
- [ ] Create capture pin
- [ ] Advertise 1280x720 @ 30 fps
- [ ] Produce generated test frame
- [ ] Verify Windows Camera application

## M3 - User-mode frame transport
- [ ] Implement driver interface
- [ ] Implement frame submission
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
