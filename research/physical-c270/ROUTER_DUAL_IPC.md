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

## Optional independent OBS reader

Owner confirmed on Windows 11 (2026-10-22) that the existing AvCam Router
preview works. That is a local application-space video validation, **not**
physical Logitech endpoint substitution.

The baseline router behavior remains unchanged. The additional
`--independent-obs` flag starts OBS capture and reconnect handling on a
dedicated worker thread; the original Logitech RAW acquisition loop no longer
waits on the OBS shared-memory queue or NV12 decoding. The flag requires
`--continuous` and cannot be combined with `--raw-only`.
It works with the same optional IPC publisher:

```powershell
.\\AvCamRouterPhysicalRawObs.exe --continuous --independent-obs --publish-ipc
```

Run `AvCamRouterPreview.exe --mode compare` separately as before. The worker
automatically invalidates the processed channel on OBS loss while the RAW
publisher continues independently, and retries OBS with an interruptible
backoff. Status logs include the OBS worker's frame and reconnect counters.
An independent camera-free test uses a deliberately delayed synthetic OBS
source to verify that RAW routing proceeds and reconnection does not alter
RAW samples. This option does not guarantee the physical source's frame rate
when other local processes contend for USB, CPU, or memory resources.

### Owner's Windows 11 acceptance log (2026-10-22)

The owner ran the **independent** mode on the main Windows 11 PC, with
`--continuous --independent-obs --publish-ipc`, and reported:

```text
AVCAM_IPC_CHANNELS=RAW,OBS
AVCAM_OBS_INDEPENDENT_WORKER=1
AVCAM_ROUTER_MODE=CONTINUOUS
AVCAM_ROUTER_STATUS raw=151 obs=151 obs_connected=1 reconnects=0
AVCAM_ROUTER_STATUS raw=302 obs=302 obs_connected=1 reconnects=0
```

These cumulative counts indicate roughly 30 frames per second over the
two five-second status periods, with neither an OBS disconnection nor
reconnection reported. The user also previously confirmed the separate
RAW/OBS preview works. **This does not establish zero dropped frames,
the pixel contents of this specific run, or Chrome substitution through
the original physical C270 endpoint.**

The subsequent router build adds periodic `raw_fps` and `obs_fps`
rates, `obs_fresh` (frame timestamp no older than 2000 ms, together
with connection state), and `obs_age_ms` (steady-clock milliseconds
since the last successful processed frame, or -1 when unavailable).
The freshness clock resets on OBS source loss and worker stop.
All counters and freshness checks are read-only diagnostics; they do
not alter or redirect captured samples.

## Live video preview (independent Windows application)

The updated no-install artifact also contains `AvCamRouterPreview.exe`.
Start the router with `--publish-ipc`, then run in a second Windows terminal:

```powershell
.\\AvCamRouterPreview.exe --mode output
.\\AvCamRouterPreview.exe --mode compare
```

Alternatively, double-click `AvCamRouterPreview.exe` to open `output`
mode. Only launch one mode at a time per window; multiple independent
viewer instances may safely attach to the same published channels.
Available modes:

- `output` (default): show a fresh OBS frame, otherwise the latest fresh
  RAW frame. If neither source is fresh, display a waiting placeholder.
- `compare`: show original RAW and processed OBS in separate panes.
  Missing streams never substitute frames from the other pane.
- `raw`: display the physical capture only.
- `obs`: display the processed OBS stream only (no RAW fallback).

The viewer is a **regular Win32 video window**, not a camera driver or a
DirectShow / Media Foundation camera source. It checks frame timestamps
against a 2000 ms freshness limit, discards invalid OBS frames promptly
when a disconnect is signaled, and reattaches following source restart.
It does not change Chrome's video feed or Deep Live Cam's input.

## Camera-independent regression test

```powershell
.\AvCamRouterIpcProbe.exe --self-test
```

Also run `AvCamPreviewSelectionTest.exe` (no camera required) to validate
freshness boundaries and the output selection/fallback decision. Windows CI
builds the native viewer and runs `AvCamRouterPreview.exe --self-test`.

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
