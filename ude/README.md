# AvCam UDE / virtual USB track

This branch explores Windows USB Device Emulation (UDE) through UdeCx rather than trying to make a root-enumerated AVStream PDO pretend to be a USB device.

## Why UDE

A root-enumerated KMDF child PDO cannot become a real USB device merely by returning `USB\\VID_xxxx&PID_xxxx` from `IRP_MN_QUERY_ID`. The supported Windows architecture is a UDE client driver backed by `UdeCx`, which exposes an emulated USB host controller and virtual USB devices through the Windows USB stack.

Microsoft's supported initialization sequence is:

1. `UdecxInitializeWdfDeviceInit`
2. `WdfDeviceCreate`
3. `UDECX_WDF_DEVICE_CONFIG_INIT`
4. `UdecxWdfDeviceAddUsbDeviceEmulation`
5. Create a virtual USB device with `UdecxUsbDeviceInitAllocate` / `UdecxUsbDeviceCreate`
6. Create endpoints and handle transfer callbacks through UdeCx
7. Call `UdecxUsbDevicePlugIn`

The WDK requirements include `Udecx.h` and `Udecxstub.lib`.

## Project boundary

The UDE track is intended for controlled lab and interoperability testing. It must not be used to misrepresent software-generated media as a physical camera to bypass identity verification, liveness, anti-spoofing, or other trust/security controls.

## Milestones

### U0 — Host controller scaffold

- Root-enumerated host controller INF (`Root\\AvCamVirtualUsbHost`)
- KMDF + UdeCx initialization
- one USB 2.0 emulated root-hub port
- private device interface for the user-mode controller/feeder
- basic plug/unplug control path

### U1 — Generic test USB function

- create a vendor-specific virtual USB device
- device/configuration/interface/endpoint descriptors
- deterministic control-transfer handling
- bulk/interrupt transfer smoke tests

### U2 — Camera-class lab device

For camera interoperability testing, use standards-compliant USB Video Class descriptors and Windows' native UVC stack. Do not manually fake `IRP_MN_QUERY_ID` on a non-USB PDO or intercept `IOCTL_INTERNAL_USB_SUBMIT_URB` on a hand-built pseudo-bus; UdeCx is the supported routing layer for emulated USB traffic.

### U3 — Media transport

- user-mode frame ingress from the existing feeder
- bounded nonpaged/ring-buffer transport
- synchronized producer/consumer state
- timeout/backpressure handling
- Driver Verifier and stress testing

## Existing AVStream work

The previous root-enumerated AVStream implementation is preserved on `dev/initial-avstream`. The UDE work lives on `dev/ude-uvc` so the two architectures can be compared without destabilizing `main`.
