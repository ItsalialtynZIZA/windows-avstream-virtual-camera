# AvCam Router: independently published RAW and OBS video frames

**Scope:** user-mode Windows 11 x64 video transport, not a virtual camera, Device MFT,
USB redirection, or Chrome-specific substitution.

## Pipeline

```text
Physical Logitech C270 (VID_046D&PID_0825) --Media Foundation--> AvCam Router
                                                            |--> RAW channel
OBS Virtual Camera --OBS shared memory, NV12 to BGRA----------> processed channel
                                                            |--> OBS channel
```

The existing source routing still verifies that its producer-facing RAW copy
never contains OBS pixels. `--publish-ipc` additionally exposes **two distinct**
read-only, same-session named-memory feeds to other AvCam processes:

| Channel | Memory mapping | Format | Disconnect |
| --- | --- | --- | --- |
| RAW | `Local\AvCamRouterRawV1` | 1280x720 BGRA, stride 5120 | Producer exits if physical read fails |
| OBS | `Local\AvCamRouterObsV1` | 1280x720 BGRA, stride 5120 | Mark invalid immediately; RAW stays available |

Each mapping contains a fixed header (magic, version, source, shape, frame
sequence, steady-clock milliseconds) and one complete 3,686,400-byte BGRA
frame. The **single writer** publishes with an odd/even epoch. Readers first
copy into private memory, then recheck the epoch and channel metadata. They
reject in-progress, incompatible and invalidated snapshots. Consumers may
skip intermediate frames; this is a latest-frame transport, not a lossless
queue. Mapping names are scoped to the Windows login session. The application
refuses to overwrite an existing writer's mapping.

## Running (no administrator, no installation)

Run OBS Studio Virtual Camera and make sure the physical C270 can be
opened concurrently on this computer. In a PowerShell console, from the
extracted no-install CI artifact folder:

```powershell
.\AvCamRouterPhysicalRawObs.exe --continuous --publish-ipc
```

For a 300-RAW-frame bounded acquisition:

```powershell
.\AvCamRouterPhysicalRawObs.exe --continuous --frames 300 --publish-ipc
```

In a **second PowerShell window while the router is running**:

```powershell
.\AvCamRouterIpcProbe.exe --channel raw --frames 30
.\AvCamRouterIpcProbe.exe --channel obs --frames 30
```

The commands are independent consumers. Use two separate windows to observe
both simultaneously. The probe copies full images and prints metadata; it
does not display video or provide a virtual webcam device. The probe exits
0 after reading the requested count of *distinct sequence numbers* or 6 on
timeout. The CLI rejects bad arguments with code 9. The router retains its
existing bounded-run semantics and returns 11 for mapping ownership/open
failure and 12 for a frame publishing failure. Without `--publish-ipc`, it
does not create the two new mappings. With `--raw-only --publish-ipc`,
only the RAW mapping is created.

## Camera-independent regression test

```powershell
.\AvCamRouterIpcProbe.exe --self-test
```

This uses synthetic RAW/OBS image data and tests isolation, caller copy
independence, OBS invalidation and recovery; it is executed by Windows CI.
It does **not** test real hardware or prove a working Chrome physical C270
substitution.

## Critical limitation

RAW and OBS images are now accessible to separate AvCam consumers when
`--publish-ipc` is enabled, but **the existing Logitech physical camera
endpoint still delivers its own original frames to Windows clients**.
No post-fanout Chrome-specific transform has been validated that also
preserves unmodified Deep Live Cam's original camera feed. Never enable
unscoped Device MFT substitution to work around this gap.
