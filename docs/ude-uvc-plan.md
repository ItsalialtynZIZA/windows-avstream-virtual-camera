# UDE/UVC architecture pivot

This branch replaces the root-enumerated AVStream camera path with a USB Device Emulation (UDE) design.

## Target stack

```text
Physical USB camera
        ↓
       OBS
        ↓
 user-mode feeder
        ↓
 UDE client driver (KMDF + UdeCx)
        ↓
 emulated USB host controller
        ↓
 virtual USB UVC device
        ↓
 Windows USB stack / usbvideo.sys
        ↓
 Windows Camera / Media Foundation / DirectShow
        ↓
 browser / application
```

The UDE controller itself is root-enumerated. The child camera device is enumerated by the Windows USB stack from the USB descriptors supplied by the UDE client driver. That child is the object expected to expose a hardware instance path in the `USB\\VID_xxxx&PID_xxxx\\...` family.

## Important constraint

Do not use a third-party company's assigned VID/PID. Development builds use placeholder constants only. Production requires a legitimate VID/PID assignment.

## Milestones

1. Build a minimal KMDF UDE controller and load it with test signing.
2. Plug in a minimal USB 2.0 virtual device and verify a child `USB\\VID_...&PID_...` node appears.
3. Add UVC VideoControl and VideoStreaming descriptors.
4. Bind the child device to the Microsoft USB video class driver (`usbvideo.sys`).
5. Implement class-specific control requests (probe/commit and required camera controls).
6. Implement the video streaming endpoint and synthetic frames.
7. Reuse the existing feeder transport so OBS frames can be delivered to the UVC IN endpoint.
8. Add runtime smoke tests, Driver Verifier coverage and production signing documentation.

## First runtime gate

Before any OBS work, the first success criterion is:

```text
UDE controller: started
Virtual USB device: enumerated
Child instance path: USB\\VID_xxxx&PID_xxxx\\...
No Code 10/39
```

Only after this gate do we implement UVC video streaming.
