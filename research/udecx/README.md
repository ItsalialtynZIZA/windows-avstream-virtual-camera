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

The portable `AvCamUdeControl.h` validates UVC class/interface request type, exact setup transfer length, backing buffer capacity, and forwards supported Probe/Commit controls to the existing host-tested `AvCamUvcProcessClassInterface` implementation. This helper is wired to the research EP0 URB callback and is compile-tested, but runtime/cancellation/IRQL behavior has not been verified.

The `tests/uvc-protocol-tests.cpp` check validates descriptor total length, alt0/alt1 layout, endpoint direction/type, Probe/Commit state, GET_LEN, wrong recipient/interface and too-short buffers. It is built by CI alongside the independent static safety checks, WDK compile and PREfast.

**Runtime blockers remain:** implementation of native USB standard-request/enum behavior as needed, cancellation-safe asynchronous ISO streaming completion, and USB enumeration plus Media Foundation verification in an isolated Windows VM. EP0 class controls are connected in source, but untested at runtime. The video endpoint still rejects URBs and auto-plug remains disabled. Do not install this unfinished driver.

## Milestone: native EP0 Probe/Commit dispatch (compile-only)

The native UdeCx endpoint queues now record USB child identity and endpoint address. The **EP0** callback retrieves the actual setup packet with `UdecxUrbRetrieveControlSetupPacket`, maps the USB transfer buffer with `UdecxUrbRetrieveBuffer`, and invokes the portable, bounded `AvCamUdeHandleUvcInterfaceControl` on per-child Probe/Commit state.

Successful control requests report their actual byte count using `UdecxUrbSetBytesCompleted` and complete via `UdecxUrbCompleteWithNtStatus`; unsupported UVC requests return `USBD_STATUS_STALL_PID` instead of reporting success. Requests for the streaming endpoint are still rejected. Every request in the current EP0 path completes synchronously, so no endpoint request is deliberately held pending.

**This is not Windows runtime verification.** No USB child is automatically plugged in; ordinary USB standard requests and native isochronous video handling are not yet implemented. A safe, cancellation-aware streaming lifecycle and first VM enumeration test remain mandatory before attempting installation.

## Milestone: bounded ISO MJPEG encoding (portable tests, no kernel URB dispatch)

The new `AvCamUdeIso.h` provides a platform-independent encoder for ISO URB packet windows. The host-provided offsets are never compacted or modified; every packet window is validated before writing. A 30fps phase-preserving MJPEG packetizer generates two-byte UVC headers, alternating FID across frames and EOF only on the final packet. It caps each packet's bytes and keeps the next frame's due time in the stream state.

`tests/iso-packet-tests.cpp` simulates more than four seconds of 128-packet USB high-speed ISO URBs against a 42,738-byte test JPEG, reconstructs full frames byte-for-byte, checks FID/EOF, measured frame count, sums of packet lengths, buffer canaries and malformed host offsets. CI runs it separately from UVC control and INF/driver compile checks.

**Important:** This is a portable data encoder, **not** a working Windows UdeCx isochronous endpoint. The kernel callback still returns `STATUS_NOT_SUPPORTED` for endpoint 0x81. Native URB metadata, per-packet completion, cancellation, reset and asynchronous completion need a separate adapter that is validated with a USB host-stack test environment.

Microsoft's UdeCx guide specifically warns that data-transfer URBs should not complete synchronously on the driver's request callback thread, and recommends completion from a separate DPC. Its individual `UdecxUrb*()` helpers document PASSIVE_LEVEL constraints. Before enabling an actual endpoint, resolve the IRQL/ownership differences using a queue-and-completion architecture, Driver Verifier, and real Windows USB enumeration. The existing EP0 implementation also currently completes inline; treat it as compile-only research until this is resolved.

## Required gates before any installation

1. WDK CI and PREfast must compile and link the controller plus the USB-device lifecycle without warnings; the static contract must ensure auto-plug stays disabled. No installer or signed device package should be published as working-camera software yet.
2. UdeCx **dynamic** endpoint construction and portable UVC Probe/Commit validation now compile and pass tests; the helper is connected to the EP0 source but synchronous completion is not yet validated, and auto-plug remains disabled. The first Windows VM runtime gate must validate enumeration under `USBHUB3` and binding to `usbvideo.sys` after an explicitly controlled enablement.
3. Implement default control endpoint and UVC Probe/Commit, streaming endpoint selection and native UdeCx URB completion methods with cancellation, reset and power handling.
4. In an isolated test environment, validate real USB device parent chain, 30fps MJPEG integrity, a nonempty Media Foundation sample, and then continuity/OBS/browser operation.

## Safety / boundaries

- Do not install this research driver on the user's primary Windows machine. The child creation/plug-in code is compiled and covered by static checks, but the power-entry auto-plug gate is intentionally disabled until UVC requests and runtime safety gates are implemented.
- Keep installed `oem61.inf` and exported `oem60`/`oem59`/`oem58` backups untouched.
- Do not merge the research branch into `main` without explicit permission.
- A software-emulated USB device can use native Windows USB class drivers; that does not make it physically connected hardware or guarantee that third-party apps cannot detect emulation.
