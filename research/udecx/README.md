# AvCam UdeCx architecture migration — controller foundation

**Status: compile-only research. Not installable as a functioning camera.**

Previous manual vHCI work constructed a KMDF-enumerated USB-like child PDO, then handled USB URBs directly. Even with 203,392 filled ISO packets and 764 internal MJPEG frame completions over 25 seconds, Windows Media Foundation did not deliver one video sample (0.1.0.6 runtime acceptance, Windows 11). More MJPEG payload pacing changes are not supported by this evidence.

Microsoft's supported mechanism for emulating an actual USB host controller and attached devices is the UdeCx class extension. It routes enumeration via the real Windows USB host stack. See:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/developing-windows-drivers-for-emulated-usb-host-controllers-and-devices
- https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/writing-a-ude-client-driver

## Foundation in this directory

- `AvCamUde.c`: initializes a UdeCx host-controller FDO, publishes the USB host-controller interface, and forwards standard host-controller IOCTLs to UdeCx.
- `AvCamUdeUsb.h` / `AvCamUdeUsb.c`: provide the new device lifecycle: high-speed UVC device/configuration/string descriptors from the established portable model, UdeCx device allocation, default control and 0x81 video endpoint queues, and explicit plug-out handling. Incomplete USB endpoint requests return an error rather than false success.
- `tests/static-contract.ps1`: checks distinct root identity, native UdeCx calls, no automatic plug-in, WDK/PREfast, and compile-only CI safety.
- `AvCamUde.vcxproj`: isolated WDK x64 KMDF project linked with `Udecxstub.lib`.
- `AvCamUde.inf`: separate, never-before-used `Root\AvCamUdeController` identity. It is deliberately **not** an update to `Root\AvCamVirtualUsbHost`.

## Required gates before any installation

1. WDK CI and PREfast must compile and link the controller plus the USB-device lifecycle without warnings; the static contract must ensure auto-plug stays disabled. No installer or signed device package should be published as working-camera software yet.
2. UdeCx child descriptor/endpoint construction now compiles, but automatic child plug-in is disabled (`g_AvCamUdeEnableAutoplug = FALSE`). The first Windows VM runtime gate must validate enumeration under `USBHUB3` and binding to `usbvideo.sys` after an explicitly controlled enablement.
3. Implement default control endpoint and UVC Probe/Commit, streaming endpoint selection and native UdeCx URB completion methods with cancellation, reset and power handling.
4. In an isolated test environment, validate real USB device parent chain, 30fps MJPEG integrity, a nonempty Media Foundation sample, and then continuity/OBS/browser operation.

## Safety / boundaries

- Do not install this research driver on the user's primary Windows machine. The child creation/plug-in code is compiled and covered by static checks, but the power-entry auto-plug gate is intentionally disabled until UVC requests and runtime safety gates are implemented.
- Keep installed `oem61.inf` and exported `oem60`/`oem59`/`oem58` backups untouched.
- Do not merge the research branch into `main` without explicit permission.
- A software-emulated USB device can use native Windows USB class drivers; that does not make it physically connected hardware or guarantee that third-party apps cannot detect emulation.
