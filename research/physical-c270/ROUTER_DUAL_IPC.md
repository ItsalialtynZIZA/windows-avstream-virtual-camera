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
| RAW | `Local\AvCamRouterRawV2` | 1280x720 BGRA, stride 5120 | Producer exits if physical read fails |
| OBS | `Local\AvCamRouterObsV2` | 1280x720 BGRA, stride 5120 | Mark invalid immediately; RAW stays available |

Each mapping contains a fixed header (magic, version, source, shape, frame
session ID, sequence, steady-clock milliseconds) and one complete
3,686,400-byte BGRA frame. The **single writer** publishes with an odd/even
epoch. Readers first copy into private memory, then recheck the epoch,
session and channel metadata. They reject in-progress, incompatible and
invalidated snapshots. Consumers may skip intermediate frames; this is a
latest-frame transport, not a lossless queue. Mapping names are scoped to the
Windows login session.

**IPC protocol V2 and crash/restart recovery:** Only a publisher retains
`Local\AvCamRouterRawOwnerV2` or `Local\AvCamRouterObsOwnerV2`, respectively.
The owner object is a single-writer lifetime lease (never created or held
by readers). A concurrent publisher fails even if launched on the same thread.
After the writer exits or crashes, readers may still hold the named mapping;
the replacement publisher safely reinitializes that mapping and assigns a
new session ID. The reader's cursor includes **both session ID and sequence**,
so frame sequence 1 in the new session is accepted even after sequence 3000
in an earlier session. RAW and OBS use separate owners and memory objects.
The prior **V1 names remain untouched**; V1 and V2 executables must be used
as matching pairs. Do not run two versions of the router against the same
physical C270 simultaneously.

**Cross-process Windows regression:** `AvCamRouterIpcProcessTest.exe --self-test`
now starts two camera-free publishers (independent RAW/OBS), verifies a
competing OBS publisher is rejected, forcibly terminates the first OBS writer,
checks that RAW continues advancing, then starts another OBS writer while the
original OBS reader still holds its shared-memory mapping open. The test
requires the replacement publisher to deliver a new session and frame data
to the retained reader. It runs automatically on Windows CI without opening
Logitech or OBS. This specifically covers abnormal writer exit, which the
earlier in-process `AvCamRouterIpcProbe.exe --self-test` could not simulate.

## One-click no-install preview (Windows)

The `AvCam-OBS-Live-Diagnostic` artifact now includes `AvCam-Start.cmd`
and `Start-AvCamPreview.ps1`. With Deep Live Cam and OBS Virtual Camera
already running, double-click `AvCam-Start.cmd` in the extracted folder.
It starts the existing independent RAW/OBS router with `--publish-ipc`,
then opens `AvCamRouterPreview.exe --mode compare`. Close the viewer window
to terminate **only the router process launched by that command**.
An already-running router may prevent the launcher from starting another
publisher; exit the earlier router first. No administrator permissions,
device installation or service registration are required.

The command file accepts additional PowerShell launch arguments, e.g.
`AvCam-Start.cmd -Mode output` to prefer OBS with RAW fallback. The
`-ValidateOnly` switch checks for the two binaries without opening either
source; Windows CI checks script syntax and validates its packaged files.
The launcher does not turn the video into the physical Logitech endpoint.

## Running (no administrator, no installation)

Run OBS Studio Virtual Camera and make sure the physical C270 can be
opened concurrently on this computer. Use the **matching router, probe
and preview binaries from the same artifact**; V2 consumers cannot subscribe
to an older V1 publisher. Keep the previous validated package as a rollback
copy. In a PowerShell console, from the extracted no-install CI artifact folder:

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

## IPC V2 to existing AvCam AVStream virtual camera feeder

The original repository already includes a **separate** experimental AVStream
virtual camera `AvCam Virtual Camera` and the user-mode `AvCamFeeder.exe`.
The feeder now accepts RAW/OBS Router **IPC V2** as direct input instead of
reopening OBS using Media Foundation or DirectShow.

With the router already running, use one of:

```powershell
.\AvCamFeeder.exe --source avcam-ipc-obs
.\AvCamFeeder.exe --source avcam-ipc-output
.\AvCamFeeder.exe --source avcam-ipc-raw
```

- `avcam-ipc-obs` reads processed OBS only; **never** substitutes RAW.
- `avcam-ipc-output` prefers processed OBS and uses RAW only on explicit
  OBS disconnect or after processed frames become stale (over 2000 ms).
  It deliberately does not substitute RAW during normal intervals between
  live OBS frames.
- `avcam-ipc-raw` reads the original physical RAW channel only.

### Safe CLI acceptance and optional virtual output

The shipping `AvCamFeeder.exe` now supports two standalone diagnostics:

```powershell
.\AvCamFeeder.exe --source avcam-ipc-obs --check-source --seconds 3
.\AvCamFeeder.exe --check-sink
```

The first command connects to an existing RAW/OBS Router V2 mapping,
accepts fresh processed OBS frames for three seconds, counts successful
samples and pixel-content changes and exits without sending frames to any
driver. It can return exit code 4 if the source is missing or 5 if no
fresh frames arrive. A successful run reports `AVCAM_IPC_CHECK frames=N`.
The second command **only enumerates** AvCam's separate, private KS sink
interface; it reports `AVCAM_VIRTUAL_SINK_PRESENT=0/1` and returns 6 when
the interface is absent. It never probes the physical Logitech C270.

The new **read-only dual-enumeration readiness** check is:

```powershell
.\AvCamFeeder.exe --check-virtual-camera
```

This checks two independent Windows API views: the private KS producer
interface and Media Foundation's video source list for the exact
`AvCam Virtual Camera` friendly name. The result is
`AVCAM_VIRTUAL_CAMERA_READINESS=ENUMERATED_NOT_ACTIVATED` (exit 0),
`ABSENT` (exit 6) or `AMBIGUOUS` (exit 7). It also prints the
enumerated interface/name counts. The command **does not activate a
camera, submit frames, modify PnP, or guarantee that Chrome accepts
the virtual camera**; the friendly name is not a proof of device
identity. If an endpoint does not appear consistently through both
enumerations, an optional virtual-output launcher fails closed.

A separate read-only transport handshake is now available:
```powershell
.\AvCamFeeder.exe --check-transport
```
This opens **only** the dedicated `GUID_DEVINTERFACE_AVCAM_FRAME_SINK`
interface and calls the repository's existing
`AVCAM_METHOD_GET_STATS` KS method. It checks whether the compatible
AvCam virtual driver's private producer interface actually responds;
it never calls `AVCAM_METHOD_SUBMIT_FRAME`. It does not open RAW/OBS
or a capture pin. Results are `RESPONSIVE` (0),
`ABSENT` (6), `AMBIGUOUS` (7),
`OPEN_FAILED` or `PROTOCOL_MISMATCH` (8).
The launcher `-VirtualOutput` now requires both this transport
handshake and the earlier two-way registration check before launching
any video input. Transport responsiveness is **not** proof of a live
video stream or successful capture from Windows Camera/Chrome.

For `--source avcam-ipc-obs --check-source` the CLI now polls new
IPC frames at higher frequency instead of sleeping 100 ms after
every ordinary inter-frame gap. This makes reported received-frame
counts more representative of the actual OBS producer cadence.


For an independently installed compatible *separate* AvCam test camera,
the no-install launcher supports an explicit opt-in:

```powershell
.\AvCam-Start.cmd -VirtualOutput
```

Without `-VirtualOutput` the launcher only opens the RAW/OBS preview.
With the flag it first runs the stricter `--check-virtual-camera`
read-only enumeration; if either the private KS interface or the
Media Foundation camera entry is missing or ambiguous, it stops with
an error **before opening Logitech**. When
present, it starts the real `AvCamFeeder.exe --source avcam-ipc-obs`
alongside the router and viewer. Closing the viewer terminates only
the child processes this launcher started. This option **does not**
install, sign, register, enable or replace any device driver. It is not
a path to impersonating the physical Logitech endpoint.

Windows CI executes the shipped feeder binary against a synthetic OBS
publisher running in a different Windows process with no installed
AvCam sink; this validates the actual executable's IPC data path,
not the kernel capture output. The remaining end-to-end separate
AVStream virtual-device test would need an explicitly prepared Windows
system with a properly installed compatible driver.

The feeder takes the latest complete BGRA frame from shared memory, converts
the captured monotonic timestamp from milliseconds to 100-nanosecond units
for its existing KS transport, then submits it through the pre-existing
`GUID_DEVINTERFACE_AVCAM_FRAME_SINK` private AVStream producer interface.

This is an **opt-in output bridge**. It can deliver to the repository's
AvCam virtual camera only if that **distinct test camera driver is already
properly installed and operational**. The no-install diagnostic package
includes the feeder executable but **does not register or install the
test-signed AVStream kernel driver**. Without a compatible existing AvCam
sink, the feeder waits rather than touching other devices. Neither mode
changes the original Logitech endpoint or achieves Chrome-specific
substitution. The test-signed driver has **not** been verified end-to-end on
the owner's main Windows machine, so the full Windows Camera/Chrome output
is not claimed as achieved.

Camera-independent CI regression `AvCamFeederIpcSourceTest` confirms
processed-only isolation, absence of inter-frame RAW flashes, explicit
fallback, stale frame rejection and restart session handling. It does not
validate a registered AVStream kernel camera.

## Critical limitation

RAW and OBS images are now accessible to separate AvCam consumers when
`--publish-ipc` is enabled, but **the existing Logitech physical camera
endpoint still delivers its own original frames to Windows clients**.
No post-fanout Chrome-specific transform has been validated that also
preserves unmodified Deep Live Cam's original camera feed. Never enable
unscoped Device MFT substitution to work around this gap.
