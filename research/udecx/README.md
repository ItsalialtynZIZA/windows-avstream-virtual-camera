# AvCam UdeCx architecture migration — controller foundation

**Status: compile-only research. Not installable as a functioning camera.**

Previous manual vHCI work constructed a KMDF-enumerated USB-like child PDO, then handled USB URBs directly. Even with 203,392 filled ISO packets and 764 internal MJPEG frame completions over 25 seconds, Windows Media Foundation did not deliver one video sample (0.1.0.6 runtime acceptance, Windows 11). More MJPEG payload pacing changes are not supported by this evidence.

Microsoft's supported mechanism for emulating an actual USB host controller and attached devices is the UdeCx class extension. It routes enumeration via the real Windows USB host stack. See:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/developing-windows-drivers-for-emulated-usb-host-controllers-and-devices
- https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/writing-a-ude-client-driver

## Foundation in this directory

- `AvCamUde.c`: initializes a UdeCx host-controller FDO, publishes the USB host-controller interface, and forwards standard host-controller IOCTLs to UdeCx.
- `AvCamUdeUsb.h` / `AvCamUdeUsb.c`: provide high-speed UVC descriptors, per-device context, UdeCx-managed dynamic EP0 / alternate-setting EP 0x81 callbacks, and explicit plug-out handling. Incomplete USB endpoint requests fail closed; video is not active.
- `tests/static-contract.ps1`: checks distinct root identity, native UdeCx calls, no automatic plug-in, WDK/PREfast, and compile-only CI safety.
- `AvCamUde.vcxproj`: isolated WDK x64 KMDF project linked with `Udecxstub.lib`.
- `AvCamUde.inf`: separate, never-before-used `Root\AvCamUdeController` identity. It is deliberately **not** an update to `Root\AvCamVirtualUsbHost`.

## Validated milestone: dynamic UVC endpoint configuration

The original UdeCx scaffold configured `UdecxEndpointTypeSimple`, although the UVC VideoStreaming descriptor declares alternate setting 0 (no endpoint) and alternate setting 1 (isochronous IN endpoint 0x81). This violates Microsoft's simple-endpoint requirements.

The driver now uses `UdecxEndpointTypeDynamic` and registers `EvtUsbDeviceDefaultEndpointAdd`, `EvtUsbDeviceEndpointAdd`, and `EvtUsbDeviceEndpointsConfigure`. UdeCx supplies each endpoint's init object when the host selects an interface setting. No video endpoint is pre-created in the idle alternate setting.

The portable `AvCamUdeControl.h` validates UVC class/interface request type, exact setup transfer length, backing buffer capacity, and forwards supported Probe/Commit controls to the existing host-tested `AvCamUvcProcessClassInterface` implementation. This reusable helper is tested, but **has not yet been wired into kernel URB dispatch**.

The `tests/uvc-protocol-tests.cpp` check validates descriptor total length, alt0/alt1 layout, endpoint direction/type, Probe/Commit state, GET_LEN, wrong recipient/interface and too-short buffers. It is built by CI alongside the independent static safety checks, WDK compile and PREfast.

**Runtime blockers remain:** a cancellation-safe, asynchronous UdeCx URB completion path; kernel binding of the UVC control helper; actual isochronous data handling; and USB enumeration testing in an isolated Windows VM. Existing endpoint callbacks still reject URBs. Automatic plug-in remains disabled. Do not install this unfinished driver.

## Required gates before any installation

1. WDK CI and PREfast must compile and link the controller plus the USB-device lifecycle without warnings; the static contract must ensure auto-plug stays disabled. No installer or signed device package should be published as working-camera software yet.
2. UdeCx **dynamic** endpoint construction and portable UVC Probe/Commit validation now compile and pass tests, but the helper is not yet connected to kernel URBs, and auto-plug remains disabled. The first Windows VM runtime gate must validate enumeration under `USBHUB3` and binding to `usbvideo.sys` after an explicitly controlled enablement.
3. Implement default control endpoint and UVC Probe/Commit, streaming endpoint selection and native UdeCx URB completion methods with cancellation, reset and power handling.
4. In an isolated test environment, validate real USB device parent chain, 30fps MJPEG integrity, a nonempty Media Foundation sample, and then continuity/OBS/browser operation.

## Safety / boundaries

- Do not install this research driver on the user's primary Windows machine. The child creation/plug-in code is compiled and covered by static checks, but the power-entry auto-plug gate is intentionally disabled until UVC requests and runtime safety gates are implemented.
- Keep installed `oem61.inf` and exported `oem60`/`oem59`/`oem58` backups untouched.
- Do not merge the research branch into `main` without explicit permission.
- A software-emulated USB device can use native Windows USB class drivers; that does not make it physically connected hardware or guarantee that third-party apps cannot detect emulation.
