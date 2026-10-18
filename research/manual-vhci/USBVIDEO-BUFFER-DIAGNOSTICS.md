# AvCam USBVideo buffer diagnostics — 2026-10-18

## Scope and safety

Development branch: `dev/manual-vhci`. Do not modify `main`. Keep CI #73 and CI #76 packages as known rollback points. CI #76 remains installed on the Windows 11 test machine; the child camera has been unplugged (`AvCamBusCtl.exe unplug`, exit code 0). CI #77 changes only the user-mode MF probe.

## Reproduced facts (not hypotheses)

- PnP: the root virtual host and the USB UVC child each reported OK while plugged in; after `unplug` the root host remains OK.
- MF probe #77: MJPG 1920x1080 at 30/1 selected successfully in one run; asynchronous `ReadSample` queued with `S_OK`, but no callback within 8 seconds. A subsequent run stalled at `ActivateObject` for 20 seconds.
- CI #76 counters before unplug: 52,071 completed isoch URBs, 26,022 URBs with data, 26,049 empty URBs, 26,022 MJPEG EOF frames, 1,664,861,538 total ISO payload bytes. These counters demonstrate driver-side construction, **not** downstream frame reception.
- ETW trace `C:\AvCamManual\etw76\capture_000001.etl`: 681 events ID 210, task `USBVideo_UserBufferStarvation`; 681 events ID 212, task `USBVideo_USBBufferOverrun`. The event payloads are empty and Windows does not resolve their templates. Do **not** equate a task name alone to a proven specific driver defect.
- Both warning types have provider GUID `{DA1D1DBD-3186-4FA2-BC2D-075EFD9E43E2}`. Media Foundation Platform emitted 22 events.
- USBVideo ETW XML decoding succeeded using `tracerpt <etl> -o decoded.xml -of XML -lr -y`; useful task names live under `RenderingInfo/Task`, not `EventData`.

## Source review (head 47e5290)

`research/manual-vhci/driver/AvCamBus.c`:
- `AvCamProcessIsochTransfer` builds one UVC MJPEG frame using up to 21 payload packets for the 63,937-byte sample JPEG, writes into each original per-packet `Offset`, and populates each descriptor's `Length` and `Status`.
- It sets `transfer->TransferBufferLength = actualBytes` (a compact sum despite sparse offsets), `transfer->ErrorCount = 0`, and synthesizes `StartFrame` for ASAP transfers.
- It delays `packets * 1250` 100-ns units (~16 ms for 128 microframes) **within** the sequential child USB queue.
- It produces a whole frame in one 128-packet URB and leaves the rest of its packets empty; observed filled and empty URBs are approximately equal.
- The UVC VS endpoint advertises 0x1400 high-speed ISO wMaxPacketSize (3 x 1024 bytes per microframe). Probe/Commit claims a payload transfer size of 3072 bytes.

## Ordered engineering work (each change independent, with rollback)

1. **Instrumentation-only build**: version stream statistics and count rejected ISO URBs by reason, requested versus returned total buffer lengths, last input/output StartFrame, per-packet capacity failures, control requests/alt-setting transitions, and time spent within the callback. Snapshot counters with proper synchronization; do not print per-URB messages.
2. **Regression tests**: assert the packet offset/length invariants, frame EOF/FID alternation, buffer ownership, zero-length packet completion, timing limits, correct 3072-byte maximum, and compatibility of the new stats IOCTL.
3. **Isolated behavioral experiments** after instrumentation: (a) preserve the original URB buffer capacity instead of shrinking the overall length, (b) separate request scheduling from completion wait, (c) pace frames to the negotiated interval and track frame delivery, (d) validate StartFrame/ASAP semantics. Change only one variable per test artifact; do not deploy based on a successful build alone.
4. **Acceptance gates**: clean WDK build, PREfast 0 warnings/errors, all unit/static tests, correct PnP and USB enumeration, MF source activation within 5 seconds, an actual nonempty MJPG sample callback within 8 seconds, continuous capture for 60 seconds without increasing USBVideo starvation/overrun warnings, reliable unplug/stop, and rollback smoke test.
5. **Operational discipline**: every experiment runs on `dev/manual-vhci`; keep build artifacts by run ID and sha256. Never force-update `main`.

## Reproduction/analysis

Capture via `logman` with `Microsoft-Windows-USBVideo`, `Microsoft-Windows-MF-FrameServer`, `Microsoft-Windows-MediaFoundation-MFReadWrite`, and `Microsoft-Windows-MediaFoundation-Platform` providers. Use `Get-WinEvent -Path <etl> -Oldest` to group events by `ProviderId`, `Id` and `Task`; export via `tracerpt` to inspect `RenderingInfo/Task` for the USBVideo tasks. The camera remains unplugged until a targeted build passes CI.
