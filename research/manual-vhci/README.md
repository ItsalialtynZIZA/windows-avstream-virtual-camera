# AvCam Manual vHCI Research Project

This branch contains an experimental KMDF software bus used to study how Windows enumerates a software-created child PDO and how USB-class style identification differs from a real USB bus stack.

## Research objective

Build the project in controlled milestones:

1. **M0 — Root bus FDO**
   - Hardware ID: `Root\\AvCamVirtualUsbHost`
   - KMDF root-enumerated bus/controller device
   - custom device interface for the user-mode control/feeder process

2. **M1 — Dynamic UVC function PDO**
   - `IOCTL_AVCAM_PLUG_DEVICE` creates one child PDO
   - Device ID / primary Hardware ID: `USB\\VID_1234&PID_5678&MI_00`
   - secondary Hardware ID: `USB\\VID_1234&PID_5678`
   - Compatible IDs: `USB\\Class_0E&SubClass_01&Prot_00`, `USB\\Class_0E&SubClass_01`, `USB\\Class_0E`
   - deterministic instance ID: `0001`
   - resulting instance path is expected to start with `USB\\VID_1234&PID_5678&MI_00\\`

3. **M2 — Internal USB request research path**
   - child default queue receives internal device-control requests
   - classify `IOCTL_INTERNAL_USB_SUBMIT_URB`
   - initial `URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE` handling
   - no claim is made that a KMDF PDO alone is equivalent to a physical USB bus

4. **M3 — Descriptor engine**
   - USB device/configuration/qualifier/string descriptors
   - UVC 1.10 VideoControl / VideoStreaming descriptor model
   - one 1920x1080 MJPEG frame descriptor at approximately 30 fps
   - descriptor model is validated by executable CTest tests before driver packaging

5. **M4 — USB/UVC state machine**
   - configuration and interface selection state
   - UVC VideoStreaming Probe/Commit controls
   - endpoint/pipe state and request cancellation

6. **M5 — Streaming experiment**
   - feeder writes frames into a bounded nonpaged frame store
   - kernel synchronization and cancellation-safe request handling
   - UVC payload header generation and transfer packetization experiments

7. **M6 — Windows integration validation**
   - inspect actual device stack with Device Manager / PnP properties / SetupAPI logs
   - verify which Microsoft class driver binds, if any
   - validate the `usbvideo.sys` hypothesis experimentally rather than assuming ID matching is sufficient
   - document limitations versus UdeCx and a physical USB host controller

## Validation gates

The GitHub Actions workflow for this branch runs these gates before publishing a test package:

- static contract tests for INF, PnP IDs, IOCTL definitions, and required driver paths;
- executable USB/UVC descriptor tests;
- user-mode `AvCamBusCtl.exe` build with `/W4 /WX`;
- WDK driver build and INF verification;
- package output checks for SYS/INF/CAT;
- PREfast with warnings treated as failures.

The artifact also contains `runtime-smoke.ps1`. That script must be run on an elevated Windows test machine because a GitHub hosted runner is not a safe or persistent kernel-driver test target. It validates root-bus start, device-interface access, child plug/unplug, instance path, hardware IDs, and UVC-compatible IDs.

## Important architectural note

Returning an ID that begins with `USB\\VID_...` does **not by itself create a true USB-enumerated device**. The PnP identity is only one part of the contract. A Microsoft USB class driver can require USB-stack semantics including configuration/interface state, internal IOCTL behavior, USBD query interfaces, transfer scheduling, cancellation, endpoint semantics, and power/PnP behavior.

For UVC interface collections, Windows uses Video class `0x0E`, VideoControl subclass `0x01`, and protocol `0x00` as the class-compatible identity. The streaming interfaces described by the configuration use subclass `0x02`.

This research branch deliberately implements the stack incrementally so that each assumption can be tested instead of hidden behind a large monolithic driver.

## Source layout

```text
research/manual-vhci/
├─ README.md
├─ shared/
│  ├─ AvCamManualIoctl.h
│  └─ AvCamUsbDescriptors.h
├─ driver/
│  ├─ AvCamBus.c
│  ├─ AvCamBus.inf
│  └─ AvCamBus.vcxproj
├─ tools/
│  ├─ AvCamBusCtl.cpp
│  └─ CMakeLists.txt
└─ tests/
   ├─ static-contract-tests.ps1
   ├─ descriptor-tests.cpp
   ├─ runtime-smoke.ps1
   └─ CMakeLists.txt
```

The existing `main` branch and the AVStream work are not modified by this research branch.

## Current diagnostic release (2026-10-18, experimental)

- Driver package 0.1.0.3 has MDL bounds checking on every USB transfer mapping,
  ISO microframe layout validation and version-3 persisted stream diagnostics.
- The video scheduler now uses microframe positions, a phase-preserving 30fps
  deadline, and accounts for processing time when waiting for the next URB.
- The MJPEG frame descriptor advertises a maximum bitrate of 100 Mbps, below
  the 196.608 Mbps theoretical high-speed 3072-byte/microframe limit.
- All portable unit tests and PREfast are required to pass in GitHub Actions.
- **No assertion of a working camera is made before an actual Media Foundation
  nonempty sample arrives on the Windows 11 test machine.**
- `tests/runtime-camera-acceptance.ps1` is a guarded elevated acceptance gate:
  it requires the child unplugged initially, uses a bounded MF probe, retains
  ETW evidence, and unplugs the child in `finally`. It does not install or remove
  the root bus or alter driver signing.
- Preserve historical CI #73 and CI #76 as rollback packages; all work stays
  on `dev/manual-vhci` and must never be merged into `main` without approval.
