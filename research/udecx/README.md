# AvCam UdeCx architecture migration — controller foundation

**Status: compile-only research. Not installable as a functioning camera.**

Previous manual vHCI work constructed a KMDF-enumerated USB-like child PDO, then handled USB URBs directly. Even with 203,392 filled ISO packets and 764 internal MJPEG frame completions over 25 seconds, Windows Media Foundation did not deliver one video sample (0.1.0.6 runtime acceptance, Windows 11). More MJPEG payload pacing changes are not supported by this evidence.

Microsoft's supported mechanism for emulating an actual USB host controller and attached devices is the UdeCx class extension. It routes enumeration via the real Windows USB host stack. See:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/developing-windows-drivers-for-emulated-usb-host-controllers-and-devices
- https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/writing-a-ude-client-driver

## Foundation in this directory

- `AvCamUde.c`: initializes a UdeCx host-controller FDO, publishes the USB host-controller interface, and forwards standard host-controller IOCTLs to UdeCx.
- `AvCamUde.vcxproj`: isolated WDK x64 KMDF project linked with `Udecxstub.lib`.
- `AvCamUde.inf`: separate, never-before-used `Root\AvCamUdeController` identity. It is deliberately **not** an update to `Root\AvCamVirtualUsbHost`.

## Required gates before any installation

1. WDK CI must compile and link the controller without warnings. No installer or signed device package should be published as working-camera software yet.
2. Implement `UDECXUSBDEVICE` creation, real USB device/configuration/string descriptors, USB 2.0 high-speed identity and clean enumeration via `USBHUB3` and `usbvideo.sys`.
3. Implement default control endpoint and UVC Probe/Commit, streaming endpoint selection and native UdeCx URB completion methods with cancellation, reset and power handling.
4. In an isolated test environment, validate real USB device parent chain, 30fps MJPEG integrity, a nonempty Media Foundation sample, and then continuity/OBS/browser operation.

## Safety / boundaries

- Do not install this controller-only driver on the user's primary Windows machine. It intentionally does not create a camera or plug a USB device.
- Keep installed `oem61.inf` and exported `oem60`/`oem59`/`oem58` backups untouched.
- Do not merge the research branch into `main` without explicit permission.
- A software-emulated USB device can use native Windows USB class drivers; that does not make it physically connected hardware or guarantee that third-party apps cannot detect emulation.
