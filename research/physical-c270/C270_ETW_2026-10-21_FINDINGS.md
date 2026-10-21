# C270 simultaneous capture: hardware ETW decision, 2026-10-21

Status: physical-endpoint concurrent capture BLOCKED; OBS IPC fanout independently validated.

## Reproduction
- Installed Deep Live Cam Studio v2.2.4 shows changing physical Logitech C270 video (VID_046D, PID_0825).
- Chrome selects **the same physical Logi C270 HD WebCam** and reports it is in use or blocked.
- Device MFT unregistered and AvCam Bridge stopped in the no-AvCam contention reproduction.
- Earlier EnableDshowRedirection=1 opt-in test was unsuccessful and restored; do not repeat.
- Microsoft-Windows-MF-FrameServer, MediaFoundation-MFCaptureEngine, MFReadWrite and Platform ETW providers captured with logman.
- ETL was written to C270-ChromeBusy_000001.etl (logman adds numbered suffix when configured as a circular session). tracerpt decoded it to CSV; neither ETL nor CSV is committed because it contains machine-specific information.

## Observed ETW evidence
- The Chrome child process PID 3920 (hex 0x0F50) was independently verified with Win32_Process, executable C:\Program Files\Google\Chrome\Application\chrome.exe, created 2026-10-20 20:04:44 local.
- That PID emitted MFCaptureEngine event 260 with 0xC00D36B9 at FILETIME 134370748425125010. Exact interpretation and causal relevance of that first code remain to be established.
- At FILETIME 134370748426144453 MFReadWrite reported 0xC00D3704; subsequent MFReadWrite Source Reader stop and MFCaptureEngine events repeat the HRESULT.
- 0xC00D3704 = MF_E_HW_MFT_FAILED_START_STREAMING. Microsoft Camera Driver Test documentation: device cannot enter run state; check state transitions and other blocking pins. This is consistent with, not exclusive proof of, hardware pin contention.
- 3306 CSV lines contain VID_046D&PID_0825 in FrameServer events, including repeated KsPin, DeviceStream, FSStream entries.
- Nearest shown C270 frame-group event before the MFReadWrite failure: FILETIME 134370748421627975 (approximately 0.45165 seconds earlier).
- Next shown C270 frame-group event after that failure: FILETIME 134370748426770647 (approximately 0.06262 seconds later).
- Thus the physical stream remained active near the Chrome failure; this does NOT prove the Frame Server was sharing successfully with Chrome, that the Deep Live Cam active backend was DShow, or that the failure was solely a USB exclusive-open refusal.
- Previous diagnostics detected FrameServer Running and FrameServerMonitor Stopped; they did not identify capture ownership.

## Engineering decision
1. Keep the original camera drivers, installed Deep Live Cam and Device MFT registration unchanged while feasibility is unresolved.
2. OBS live ingestion and independent AvCam IPC frame consumer fanout are already hardware-tested: each reader saw 90/90 frames at ~30 fps. Do not spend further iterations repeating that working subsystem.
3. **Do not claim** a user-mode single-owner broker solves this. Two unmodified applications independently opening the same exclusive physical capture pin cannot be fixed by publishing mock/OBS frames after the open has already failed.
4. A Device MFT may substitute images after source acquisition succeeds but does not grant Chrome another hardware capture handle.
5. Gate implementation on an evidenced Windows-supported way to back the **existing USB C270 camera endpoint** with a shared/no-second-hardware-open frame source that leaves Deep Live Cam's unmodified selection and preview working. A separately enumerated virtual/software camera is *not* this endpoint.
6. If such a supported endpoint integration cannot be established, explicitly mark strict acceptance as blocked and require a conscious scope change instead of silently switching Chrome to a new virtual device.
7. No driver deployment or hooking system processes without an independently reviewed scope, explicit owner approval and rollback.

## References
- Microsoft camera HLK error explanation: https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/eb76bdd3-edc2-484b-9344-ac26c15a8a2c
- Microsoft DShow Bridge guidance: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dshow-bridge-implementation-guidance-for-usb-video-class-devices
- Microsoft Frame Server Custom Media Source: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source
- See CAPTURE_SHARING_DECISION.md and SINGLE_OWNER_CAPTURE_RFC.md for acceptance gates and prior tests.
