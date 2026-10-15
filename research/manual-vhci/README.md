# AvCam Manual vHCI Research Project

This branch contains an experimental KMDF software bus used to study how Windows enumerates a software-created child PDO and how USB-class style identification differs from a real USB bus stack.

## Research objective

Build the project in controlled milestones:

1. **M0 — Root bus FDO**
   - Hardware ID: `Root\\AvCamVirtualUsbHost`
   - KMDF root-enumerated bus/controller device
   - custom device interface for the user-mode feeder

2. **M1 — Dynamic child PDO**
   - `IOCTL_AVCAM_PLUG_DEVICE` creates one child PDO
   - Device ID / Hardware ID: `USB\\VID_1234&PID_5678`
   - Compatible ID: `USB\\Class_0e&SubClass_03&Prot_00`
   - deterministic instance ID: `0001`

3. **M2 — Internal USB request research path**
   - child default queue receives internal device-control requests
   - instrument and classify `IOCTL_INTERNAL_USB_SUBMIT_URB`
   - no claim is made that a KMDF PDO alone is equivalent to a physical USB bus

4. **M3 — Descriptor engine**
   - USB device/configuration/string descriptors
   - UVC VideoControl / VideoStreaming descriptor model
   - protocol state machine for GET_DESCRIPTOR and UVC class requests

5. **M4 — Streaming experiment**
   - feeder writes frames into a bounded nonpaged frame store
   - kernel synchronization and cancellation-safe request handling
   - packetization experiments for video transfers

6. **M5 — Windows integration validation**
   - inspect actual device stack with Device Manager / WinObj / SetupAPI logs
   - verify which Microsoft class driver binds, if any
   - document limitations versus UdeCx and a physical USB host controller

## Important architectural note

Returning an ID that begins with `USB\\VID_...` does **not by itself create a true USB-enumerated device**. The PnP identity is only one part of the contract. A production USB class driver expects USB-stack semantics, internal IOCTL behavior, configuration/interface state, transfer scheduling, cancellation, endpoint semantics, and power/PnP behavior.

This research branch deliberately implements the stack incrementally so that each assumption can be tested instead of hidden behind a large monolithic driver.

## Initial source layout

```text
research/manual-vhci/
├─ README.md
├─ shared/
│  └─ AvCamManualIoctl.h
└─ driver/
   ├─ AvCamBus.c
   └─ AvCamBus.inf
```

The existing `main` branch and the AVStream work are not modified by this research branch.
