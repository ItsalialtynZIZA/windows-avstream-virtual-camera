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

## Milestone: native ISO URB preflight + cancellation ownership model

The kernel now builds and calls `AvCamUdeInspectIsoUrb` from the UdeCx video endpoint `0x81` before rejecting unsupported streaming requests. It reads the incoming WDF request parameters, requires `URB_FUNCTION_ISOCH_TRANSFER` and an IN direction, checks the variable-length isochronous packet array against `UrbHeader.Length`, obtains the real mapped transfer buffer through `UdecxUrbRetrieveBuffer`, and validates **every packet window** for ordering, size and bounds. The USB device uses a passive-execution WDFDEVICE because the UdeCx transfer-buffer helper requires `PASSIVE_LEVEL`.

These checks are covered by an independent native-header simulation (`iso-urb-layout-tests.cpp`) including truncated headers, oversized packet counts, invalid offsets, incorrect direction, and transfer-buffer mismatches.

A separate portable `AvCamUdeRequestLifecycle.h` models cancellation while a worker still owns a buffer and permits one completion claim only after the worker has relinquished ownership. `request-lifecycle-tests.cpp` verifies cancellation-before-work-end, cancellation-before-completion, and duplicate completion prevention. **The model is intentionally not thread-safe and is not yet used to complete kernel requests**; the production WDF implementation must serialize transitions, connect `EvtIoCanceledOnQueue` to a deferred completion path, and independently verify DPC/PASSIVE_LEVEL requirements with Driver Verifier.

**Streaming still fails closed.** A validated ISO URB currently receives `STATUS_NOT_SUPPORTED`; `AvCamUdeIsoFill` is not yet called on a live URB, no input JPEG source exists on the new UdeCx device, and the child auto-plug gate stays disabled. CI passing does not mean Windows has enumerated the camera or decoded frames.

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


## Staged URB adapter and immutable frame-source leases (not live)

`AvCamUdeIsoAdapter.c` now builds with the WDK. `AvCamUdePrepareIsoUrb`
validates an actual incoming IN ISO URB using the preflight routine, maps its
transfer buffer at PASSIVE_LEVEL, allocates bounded nonpaged workspace, and
calls the portable `AvCamUdeIsoFill` packetizer. On success only, it writes
each original `IsoPacket[].Length/Status` and returns the sparse sum of
transferred bytes. It does **not** complete or queue the WDF request; the
caller must own it exclusively, serialize the stream state, publish actual
completion bytes through UdeCx, and complete asynchronously with full
cancel/reset/purge/power handling. Endpoint `0x81` still rejects requests.

`AvCamUdeFrameSource.h` provides a portable two-slot, caller-buffered MJPEG
source model. Publishing copies into an unleased inactive slot, and acquiring
a lease pins immutable bytes until the consumer releases it. Both buffer
ranges must be disjoint, and source operations must be serialized by a
future kernel lock. The lease must stay valid over the entire JPEG frame,
not just one URB; a real feeder interface and WDF locking are **not yet
connected**. The host-only `frame-source-tests.cpp` covers publication,
buffer pinning, overlapping allocations and busy-slot behavior.

**Known runtime blockers:** real decodable 1920x1080 JPEG ingress, wiring
frame leases across ISO URBs, handling endpoint START/PURGE/RESET with
cancellation-safe ownership, and a verified UdeCx completion strategy.
Microsoft documents PASSIVE_LEVEL for `UdecxUrbRetrieveBuffer`,
`UdecxUrbSetBytesCompleted` and `UdecxUrbComplete`; a naive completion from
the endpoint callback/DPC is not valid. Do not enable auto-plug or install
this scaffold on the primary Windows 11 machine.

## Endpoint START/PURGE callbacks (compile-only)

The dynamic endpoint registration now includes explicit
`EvtUsbEndpointStart` and `EvtUsbEndpointPurge` callbacks. This version
has only the UdeCx-managed endpoint queue: no request is forwarded or
retained asynchronously, so purge calls `UdecxUsbEndpointPurgeComplete`
directly. The start callback does not call `WdfIoQueueStart`; UdeCx owns the
state of its associated queue. This implementation MUST be revisited before
enabling a worker/forwarding queue: purge will then need to await a drained
and cancellation-safe worker path rather than acknowledge immediately.


## PASSIVE_LEVEL purge handling and active request drain

The native endpoint now owns a WDF spinlock, WDF work item, and
`AVCAM_UDE_ENDPOINT_DRAIN` state model (`AvCamUdeEndpointDrain.h`).
The synchronous callback is counted from admission to its return, including
its inline USB completion. PURGE immediately stops new admissions and waits
for the active count to become zero. Only then is a work item queued; its
PASSIVE_LEVEL callback claims a single acknowledgement and calls
`UdecxUsbEndpointPurgeComplete` outside the spinlock. This addresses the
IRQL mismatch between a UdeCx purge callback (up to DISPATCH_LEVEL) and
`UdecxUsbEndpointPurgeComplete` (PASSIVE_LEVEL).

The six portable test suites include `endpoint-drain-tests.cpp`, which
checks duplicate purge, START barriers, active I/O draining, generation
overflow and 5^7 bounded interleavings. This does not validate WDF
lifetime races under Driver Verifier. An async worker must hold the active
count until its request and mapped buffer are no longer accessed; it must
not simply decrement at callback return.

## Cross-URB immutable MJPEG source pump

`AvCamUdeFramePump.h` composes the previously implemented source leases
with `AvCamUdeIsoFill`, validating all host packet windows up front.
The current JPEG lease remains pinned across any number of ISO URBs and is
released only after the encoder has emitted UVC EOF. The next JPEG can be
selected at a subsequent microframe boundary, including within the same
URB. The pump retains FID, frame interval, and sparse offset semantics.
`AvCamUdePrepareIsoUrbFromPump` stages this path against actual UdeCx
buffers and packet descriptors, but **is not called by endpoint 0x81**.

`frame-pump-tests.cpp` simulates a change of source JPEG while a prior
JPEG is mid-stream and rejects slot overwrites until EOF. It reconstructs
the resulting frame generations from ISO payloads and verifies canaries,
FID/EOF boundaries and byte equality. These are synthetic marker-terminated
byte arrays, not evidence of a decodable 1920x1080 JPEG.

Still blocked before live video: a validated user-mode JPEG ingress path
and decoder-compatible samples, proper mapping of host USB frame timing,
an asynchronous cancel-safe WDF request pipeline with completion-thread
verification, endpoint reset/power teardown, and isolated Windows VM
USBVIDEO/Media Foundation acceptance. Do not install the research driver
or enable automatic USB child enumeration on the primary PC.


## OBS / Media Foundation -> WIC MJPEG producer (isolated research tool)

`tools/AvCamUdeObsFeeder.cpp` captures from a Media Foundation source whose
friendly name contains `OBS Virtual Camera`, using the already existing
`feeder/src/MediaFoundationFrameSource.cpp` implementation. The capture is
1280x720 BGRA32; `tools/AvCamUdeMjpegEncoder.cpp` scales it to the advertised
1920x1080 size with Windows Imaging Component, converts it to 24bpp BGR,
encodes a **real** JPEG and checks its SOF size and maximum frame bytes before
submitting it via the private JPEG upload IOCTL. Upload is permitted only to
the research controller, which itself refuses frames when the experimental
UdeCx USB child is not plugged in.

The tool's `--dry-run` option exercises Media Foundation capture and WIC
JPEG encoding without opening or sending anything to a device:

```powershell
AvCamUdeObsFeeder.exe --device-name "OBS Virtual Camera" --frames 120 --dry-run
```

This is optional for a future **isolated VM** where OBS capture is already
available. The source tool is compiled by CI but never run by CI, installed
or distributed as a signed device package. CI separately runs a generated
BGRA32 -> WIC 1080p JPEG -> WIC decoder regression without OBS/hardware.
A successful dry-run or upload does **not** establish that usbvideo.sys has
enumerated the UVC child or that Media Foundation has read any USB frame.

**Unresolved primary blocker:** UdeCx URB completion/cancel semantics at
PASSIVE_LEVEL versus the separate-DPC recommendation, plus handling UVC
stream reset/USB scheduling correctly. Endpoint 0x81 still rejects URBs.
Do not enable child autoplug or install the research driver on the host PC.

## Bounded async ISO cancellation/purge/reset model (no live endpoint yet)

`AvCamUdeAsyncIso.h` now models a fixed, **eight-request maximum**
asynchronous ISO outstanding set, with non-reusable 64-bit tickets,
worker-start/worker-finished ownership transitions, cancellation, purge
acknowledgement, and generation invalidation across reset. Source-frame
buffers cannot be released by a cancellation path while a worker still owns
them. The new `async-iso-scheduler-tests.cpp` explores adversarial state
interleavings, including cancellation before and during an encode, a full
queue, purge while one URB is active, reset while an URB is active, and
stale-token/double-completion rejection.

This is **not** a WDF request manager. In particular, a native implementation
must still pair `WdfRequestMarkCancelableEx` with the correct
`WdfRequestUnmarkCancelable` outcome, implement EvtIoCanceledOnQueue where
needed, retain requests across workers, and obey the documented completion
IRQL/dispatch contract. The UdeCx API documentation states PASSIVE_LEVEL
for `UdecxUrbComplete` and `UdecxUrbCompleteWithNtStatus`, while the
UdeCx client-driver guide recommends completing host URBs via a separate
DISPATCH_LEVEL DPC. **Do not enable EP 0x81 based on this model alone.**

The native `AvCamUdeEvtEndpointReset` now checks the same counted active
request state protected by its endpoint spinlock. If a request still owns a
buffer or purge is in progress, reset returns `STATUS_DEVICE_BUSY` rather
than a false success. An idle reset increments the endpoint generation.
This is safe for the current synchronous fail-closed queue; once asynchronous
workers are introduced, they must extend the active barrier before reset can
succeed. Do not install this experimental driver on the primary machine.

### Completion callback return barrier

The asynchronous ownership model now distinguishes `READY` (worker no
longer touches the mapped URB) from `COMPLETING` (a single native completion
owner has been claimed). **Claiming completion does not decrement
`Drain.Active`.** The native adapter must call
`AvCamUdeAsyncCompletionReturned` only after its WDF/UdeCx completion API
has returned, without accessing the potentially freed WDFREQUEST again.
This prevents PURGE or RESET from considering the endpoint idle while
the completion call is still unwinding and triggering reentrant callbacks.
The model's cancellation decision is immutable once claimed.

### Native PASSIVE-level ISO completion metadata stage

`AvCamUdeSealPreparedIsoUrbPassive` now validates every staged ISO IN
packet's actual length against its original host-defined window, checks
that every packet reports USBD success, and verifies the sparse sum equals
the caller's reported transfer count. Only then does it call
`UdecxUrbSetBytesCompleted` at `PASSIVE_LEVEL`, updating URB success
metadata without completing or relinquishing the WDFREQUEST.

The portable `AvCamUdeIsoCompletionValid` and
`iso-completion-tests.cpp` specifically reject mistaken reporting of
TransferBufferLength (capacity) instead of the sum of individual payload
lengths, overlapping offsets, packet overruns, and overflowed counts.

This moves the isolated preparation path closer to actual UdeCx completion
but does **not** install/enable video endpoint 0x81 or settle the DPC versus
PASSIVE_LEVEL completion contract. Native async cancellation ownership,
worker/DPC completion and runtime USBVIDEO verification are still required.

## Compile-only native ISO DPC completion bridge

`AvCamUdeIsoNative.c` now supplies a real **native kernel** integration
path. It validates ISO URB packet layout, checks a negotiated UVC Commit
and source availability, stages the MJPEG payload via
`AvCamUdePrepareIsoUrbFromPump`, seals sparse transfer lengths via
`UdecxUrbSetBytesCompleted` at `PASSIVE_LEVEL`, and queues a distinct
`WDFDPC` for `WdfRequestComplete` at `DISPATCH_LEVEL`. The sequential
UdeCx endpoint queue retains one pending URB. The DPC tests cancellation
with `WdfRequestIsCanceled` before completion, and only decrements the
endpoint's drain count **after** the completion call returns. A purge
therefore cannot acknowledge an owned URB before that last return.

The UVC Probe/Commit state is now read and modified under the same
`FrameLock` as MJPEG publication/staging. The pump's time base is currently
a synthetic monotonic microframe count and is not yet calibrated against
the actual host URB `StartFrame`/frame-number scheduler.

**Safety gate:** `g_AvCamUdeEnableNativeIso = FALSE` and
`g_AvCamUdeEnableAutoplug = FALSE` remain in native source. The new
bridge is compiled/analyzed by GitHub Actions but **will not execute
against a real USB host** in these builds. Before enabling it, prove
callback reentrancy, WDF cancellation/purge during queued DPC completion,
usbvideo.sys isochronous frame timing and Driver Verifier behavior in an
isolated VM. No installed host driver is altered by this milestone.

### Dynamic UVC alternate-setting endpoint lifetime

A temporary implementation dereferenced the cached
`controller->VideoEndpoint` during `EvtEndpointsConfigure`; this is unsafe
because UdeCx may already have deleted that dynamic endpoint. The callback
now only acknowledges configuration and does not dereference the cached
handle. Each endpoint's own `EvtUsbEndpointStart` sets
`PumpResetPending` under its spinlock, and its next PASSIVE ISO worker
resets the MJPEG lease, packetizer FID/EOF and stream clock. PURGE and
idle RESET also invalidate the pump. All of this remains within the
disabled, compile-only native ISO bridge pending VM/runtime validation.

## Host USB StartFrame ordering and DPC handoff hardening

`AvCamUdeIsoClock.h` interprets the high-speed host `StartFrame` field
as a **1-ms USB frame**, advances eight 125-us ISO packets per frame,
tracks explicit frame order across the 2048-frame wrap, and rejects stale
(backward) windows. For `USBD_START_ISO_TRANSFER_ASAP`, the input
StartFrame is not treated as a usable timestamp: the emulated stream advances
on its monotonic microframe timeline instead. The clock is committed only
after both ISO packet staging and PASSIVE_LEVEL UdeCx metadata sealing
succeed. A failed URB, endpoint START, PURGE, or reset invalidates it.

In `AvCamUdeIsoNative.c` a completed WDF request may reenter the sequential
endpoint queue while its original DPC is still running. The DPC now loops
over handoffs, and the next request is reserved and the completion DPC
enqueued under one endpoint spinlock. An already-running DPC consumes a
newly handed-off request without attempting to enqueue itself again. On
cancelled completion the next frame pump is invalidated, to avoid continuing
a JPEG whose EOF never reached the host.

**Important remaining requirements:** live USB frame counter and late-ASAP
behavior still require VM validation; queued-on-cancelled URB callback
semantics, PnP removal and DPC teardown need Driver Verifier coverage.
Both `g_AvCamUdeEnableNativeIso` and
`g_AvCamUdeEnableAutoplug` remain FALSE.

### Canceled ISO packet metadata

A pending ISO URB might have successful MJPEG bytes staged and sealed at
PASSIVE_LEVEL, then be canceled before the separate completion DPC runs.
The DPC now marks the URB and each ISO packet with `USBD_STATUS_CANCELED`,
clears the reported transfer length, and records canceled packets in
`ErrorCount` before completing with `STATUS_CANCELLED`. The next request
will reset its MJPEG packetizer. A temporarily absent feeder no longer
consumes a pending packetizer reset. This code remains guarded by the
disabled native ISO execution flag; only kernel compilation and PREfast
have been exercised, not runtime behavior under USBVIDEO/Driver Verifier.

### VS COMMIT renegotiation and sealed-but-undelivered URBs

The native EP0 handler now advances a 64-bit `CommitEpoch` after each
successful UVC VideoStreaming `SET_CUR(COMMIT)`, under the same
`FrameLock` that serializes ISO staging and JPEG publication. The video
endpoint remembers `SeenCommitEpoch` and discards its old MJPEG frame
lease and scheduling clock if the committed generation changes. It does
not dereference a potentially deleted dynamic endpoint from the EP0 path.
The portable frame-pump regression now covers partial old JPEG → new
published JPEG → stream reset → new SOI, with no leaked leases.

A second failure path is also handled: if a fully staged and sealed URB
cannot be handed to the DPC (endpoint PURGE or failed DPC enqueue), the
next ISO worker resets the frame pump. It must never continue streaming
JPEG bytes from a request that never reached the USB host.

**Still intentionally blocked:** complete canceled-on-queue handling,
formal WDF cancellation ownership, final teardown with a leased frame,
and validation under Driver Verifier in an isolated Windows VM. Compile
and static-analysis success is not proof of a usable USB Video Class
device or a Media Foundation sample.

### Dynamic endpoint teardown releases in-progress JPEG

The endpoint's PASSIVE_LEVEL purge work item now releases any active
MJPEG frame lease under the controller's `FrameLock` **after the last
endpoint-owned URB has completed and before**
`UdecxUsbEndpointPurgeComplete`. This prevents a deleted dynamic
alternate-setting endpoint from leaving the shared source slot permanently
pinned when a JPEG was interrupted before EOF. The endpoint retains its
parent controller identity explicitly instead of relying on the cached
`VideoEndpoint` pointer, which can be stale after deletion.

The frame-pump integration test now models a partial JPEG, drain barrier,
purge acknowledgement, frame release, subsequent publication, and restart
with a new JPEG SOI. Driver Verifier and runtime teardown validation are
still required before changing the disabled execution gates.

## Queue cancellation completion and dynamic endpoint-owned queues

The UdeCx client-driver guide requires canceled-on-queue URBs to be
completed from a **separate DPC**, not inline from
`EvtIoCanceledOnQueue`. The native endpoint queue now registers that
callback and `AvCamUdeQueueCancel.c` defers canceled requests to its own
endpoint-scoped WDF DPC. The DPC sets `USBD_STATUS_CANCELED` and
`STATUS_CANCELLED`. It decrements the separate cancellation-outstanding
count **only after** `WdfRequestComplete` has returned. The endpoint
`PURGE` work item waits for this count as well as ordinary active I/O.

The queue is now created **after** `UdecxUsbEndpointCreate` and sets
`queueAttributes.ParentObject = (WDFOBJECT)*endpoint`. This follows
Microsoft's UdeCx guidance for dynamically created endpoints and prevents
an old controller-owned WDF queue from retaining a stale handle after
alternate-setting changes. Child work items and DPCs likewise belong to
their endpoint.

The subsequent allocation-free FIFO supersedes the original per-cancel
allocation path. Its use of WDF completion information as a temporary
private next-request link still requires Driver Verifier validation.
Potential cancellation/PURGE callback ordering and device removal remain
**VM Driver Verifier release blockers**; the new code must not be enabled
on the primary computer. Neither native ISO nor child autoplug are enabled.


### Allocation-free queued-cancellation completion

The canceled-on-queue handler now keeps a single-linked FIFO of WDFREQUEST
handles, without `ExAllocatePool2` or per-request kernel allocations.
While each canceled request is exclusively owned by the endpoint, its
driver-defined `WdfRequestSetInformation` field temporarily points to the
next request; the DPC restores that completion information to **zero**
before passing the request to the USB stack with
`WdfRequestCompleteWithInformation(..., STATUS_CANCELLED, 0)`.
This removes the former immediate-completion fallback when nonpaged
pool allocation failed. The dedicated DPC still decrements
`QueueCancelOutstanding` only after completion returns; PURGE retains
the existing final-I/O barrier.

This relies on the WDF completion-information field remaining private
until completion, and must be challenged by Driver Verifier, cancellation
storms and device removal in the isolated Windows 11 VM. It is not an
authorization to enable either experimental deployment flag.

### Closing the completion-DPC / PURGE lifetime gap

A zero active-request count does not guarantee the DPC callback has
fully returned: the callback may still be unwinding after the last
`WdfRequestComplete`. Before acknowledging UdeCx endpoint PURGE, its
PASSIVE_LEVEL work item now confirms zero active ISO and queued-cancel
counts, then uses `WdfDpcCancel(..., TRUE)` to wait for **both**
endpoint DPC callbacks to quiesce. After waiting, it reacquires the
endpoint spinlock and checks again. If a new queued cancellation arrived
during the wait, it reschedules that DPC and defers PURGE.
`UdecxUsbEndpointPurgeComplete` remains outside all WDF locks.

This requires verification under real UdeCx endpoint teardown, especially
the exact ordering of cancel callbacks that might race with the final
PURGE claim. The behavior remains disabled outside the isolated test VM.
