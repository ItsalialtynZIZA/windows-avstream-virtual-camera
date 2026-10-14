# Windows AVStream Virtual Camera

Experimental Windows 11 x64 virtual camera implemented as an AVStream camera driver plus a user-mode frame feeder.

## Target pipeline

```text
Physical USB Camera
        |
        v
       OBS
        |
        v
 OBS Virtual Camera
        |
        v
 Media Foundation frame source
        |
        v
 AvCamFeeder.exe
        |
        | private KS frame transport
        v
 AVStream virtual camera driver
        |
        v
 Windows Camera subsystem
        |
        v
 Windows Camera / Chrome / Edge / desktop application
```

## What the project is

AvCam creates a Windows camera capture device named `AvCam Virtual Camera`.

Current camera mode:

- 1280x720
- approximately 30 fps
- top-down RGB32/BGRA32
- synthetic fallback image when no producer frame is available

The user-mode feeder can currently use either a generated test pattern or a Media Foundation video-capture device such as OBS Virtual Camera.

## What the project is not

This project is **not** USB/UVC bus emulation. It does not claim a physical USB VID/PID, USB link speed, UVC descriptors, or other physical-bus identity. It is a Windows AVStream camera capture device.

## Repository layout

```text
driver/     AVStream minidriver, capture pin, KS transport, frame store
feeder/     User-mode producer and Media Foundation bridge
shared/     Shared producer/driver protocol
scripts/    Build/install/verify/test-signing helpers
docs/       Architecture, roadmap and Windows runtime test plan
.github/    Windows CI
```

## Development target

- Windows 11 x64
- Visual Studio 2022
- Windows Driver Kit / SDK 26100 family
- C++17
- GitHub Actions `windows-2022` for compile/package validation

## Current implementation status

### Driver

- `KsInitializeDriver` AVStream entry point
- root-enumerated `Root\AvCamVirtualCamera`
- Camera/Video/Capture KS categories
- one 1280x720 RGB32 capture pin
- strict data-format validation
- test-signed INF/CAT/SYS package generated in CI
- synthetic fallback frame
- private KS method set for producer frame submission
- three-slot fixed nonpaged latest-frame store
- rundown protection for frame-store teardown
- submitted/replaced/dropped/capture/fallback counters

### Feeder

- producer discovery through SetupAPI
- AvCam filter detection by private KS method probing
- automatic transport reconnect
- `IFrameSource` abstraction
- deterministic 30 fps test-pattern source
- Media Foundation capture source
- case-insensitive video-source friendly-name matching
- Source Reader advanced video processing
- requested RGB32 1280x720 @ 30 fps output
- negotiated-output validation
- `IMF2DBuffer` pitch-aware copy
- source reconnect if OBS/capture source disappears

## Feeder commands

List Media Foundation video capture sources:

```powershell
.\AvCamFeeder.exe --list-sources
```

Stream the deterministic test pattern:

```powershell
.\AvCamFeeder.exe --source test
```

Bridge OBS Virtual Camera into AvCam:

```powershell
.\AvCamFeeder.exe --source obs
```

Use a specific Media Foundation friendly-name substring:

```powershell
.\AvCamFeeder.exe --source obs --device-name "OBS Virtual Camera"
```

## CI artifacts

A successful `Validate` workflow publishes:

- `AvCam-test-package-x64`
  - `AvCam.sys`
  - `AvCam.inf`
  - `AvCam.cat`
  - test certificate
  - Windows install/verify/uninstall helpers
  - runtime test documentation
- `AvCamFeeder-x64`
  - `AvCamFeeder.exe`

## Runtime verification

Compilation and packaging are automated, but kernel/runtime behavior must still be validated on a Windows 11 x64 development/test machine.

See:

```text
docs/local-test.md
```

The runtime sequence is intentionally staged:

1. confirm PnP enumeration;
2. confirm synthetic video in Windows Camera;
3. confirm user-mode test-pattern transport;
4. confirm OBS Virtual Camera -> AvCam transport;
5. confirm Chrome/Edge enumeration;
6. then run Driver Verifier and additional hardening tests.

## Safety note for driver development

The CI package is test-signed. Do not weaken driver-signing policy on a production machine. The repository installer does not enable Windows test-signing automatically and only imports the test certificate when explicitly requested.
