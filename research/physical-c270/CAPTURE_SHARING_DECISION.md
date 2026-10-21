# Physical Logitech C270: capture-sharing architecture decision

Status: investigation; NOT a working fix for simultaneous Deep Live Cam + Chrome.

## Hardware acceptance evidence (owner's Windows)
- Logitech C270 USB VID_046D PID_0825 is the selected physical device.
- With Deep Live Cam v2.2.4 closed, the experimental Device MFT passes the
  OBS red-test scene to Windows Camera and Chrome, still listed as Logitech.
- With Device MFT disabled and Bridge stopped, Deep Live Cam opens the
  physical C270, but Chrome reports it is used or blocked while Deep Live
  Cam runs. Thus simultaneous capture fails without AvCam in the pipeline.
- Opening Deep Live Cam before Bridge publication leaves Deep Live Cam running,
  but Chrome remains unable to open the physical C270.
- Device MFT ETW events establish InitializeTransform, MJPG 1280x720 format
  negotiation and sample replacement; they do not establish Chrome sharing.

## Source review: CRSD-Lau/deep-live-cam tag v2.2.4
- modules/video_capture.py enumerates with DirectShow via pygrabber;
  cv2.VideoCapture(..., cv2.CAP_DSHOW, params) is tried first.
  CAP_MSMF / CAP_ANY are fallback backends and may enumerate in a different
  order. The initial capture request is configurable; the method default
  is 960x540 at 60 FPS.
- modules/ui.py starts VideoCapturer in _start_live_capture.
- Its own pyvirtualcam sink is separate from the physical capture operation.
- The installed release is not being modified.

## Microsoft camera documentation
- DShow Bridge is the Windows bridge between DirectShow and the Media
  Foundation camera pipeline. A compatible opt-in and Frame Server can
  allow concurrent capture, but this does not imply all legacy DShow
  applications or all UVC drivers share a stream:
  https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dshow-bridge-implementation-guidance-for-usb-video-class-devices
- Custom Frame Server sources can advertise shareable streams, but registering
  a NEW software camera is not equivalent to changing the physical C270:
  https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source

## Completed C270 DShow Bridge hardware experiment (2026-10-21)
- Device MFT registration was confirmed absent, C270 present and OK,
  FrameServer running, and EnableDshowRedirection initially absent.
- The opt-in DShow Bridge trial wrote bit 0x1 to the C270's MI_00 device
  parameters and persisted a per-device rollback snapshot.
- After reconnecting C270, Deep Live Cam live processing worked, but Chrome
  still reported the physical camera was used/blocked.
- The owner executed -Restore; it reported RESTORED_ORIGINAL_DSHOW_VALUE,
  retaining the backup. This is evidence that restore code succeeded,
  not a separate post-reconnect registry inspection.
- Outcome: this opt-in did NOT solve simultaneous capture. Do not repeat
  this toggle as a purported fix or enable it automatically.

## Owner decision: strict physical C270 endpoint + single-owner capture
Owner selected architecture A. The Chrome selector must remain the existing
physical Logi C270 HD WebCam, not a newly registered software device.

Implementation gate before writing a capture broker:
- A process that exclusively opens C270 and distributes frames cannot make
  legacy Deep Live Cam open that already-occupied physical C270.
- A new software camera may feed the unmodified Deep Live Cam if the user
  changes its selected input, but does not preserve the C270 source selection
  there, and the existing Chrome physical endpoint would still need a
  separately implementable no-hardware-frame Server/KS integration.
- Device MFT sample substitution only works after the original endpoint
  opens. The C270 simultaneous DirectShow/Chrome open currently fails.
- Do not implement an application-only broker and report it as fulfillment
  of the strict physical-endpoint contract.
- Next proof-of-feasibility must show a supported OS capture route that
  can open the existing physical endpoint for Chrome while the sole
  hardware capture owner is active. Without this, the contract is unproven.

## Next implementation decision
The existing original physical C270 endpoint cannot serve both consumers in
the observed mode. Choose explicitly between two acceptance contracts:
(A) Strict physical C270 endpoint in Chrome: research a validated shared
    Windows camera capture integration. There is NO verified implementation
    meeting this contract yet, and a generic application-only broker does
    not meet it.
(B) Processed video in Chrome under a separate virtual camera: use original
    Deep Live Cam preview -> OBS Window Capture -> OBS Virtual Camera; this
    can avoid Chrome's second open of physical C270. This is an optional
    functional fallback, NOT equivalent to the physical identity contract.
Do not claim (B) satisfies (A). Never propose masking a software camera as
a physical USB descriptor or changing USB VID/PID as a verified solution.

## Architectural constraints and next engineering steps
1. Keep the physical Logitech and unmodified Deep Live Cam functional.
   Do not toggle system-wide Frame Server registry switches or replace
   USB descriptors/drivers based on a concurrency hypothesis.
2. Diagnose whether the C270's DirectShow capture pin is routed through
   Windows DShow Bridge or directly consumes the KS pin. Record backend,
   format, HRESULT, and Frame Server status while reproducing both clients.
3. If the current C270 driver/DirectShow path cannot share with Chrome,
   neither AvCam Bridge nor a sample-editing Device MFT can force a second
   physical capture handle to succeed. App-level splitters need clients
   to consume the splitter's endpoint; they cannot transparently make
   two existing physical-hardware opens succeed.
4. A distinct virtual endpoint and single-owner capture broker is a viable
   separate engineering design, but it does not meet the stricter
   requirement of selecting the already-existing physical C270 in Chrome
   without an additional compatible OS-level integration.
5. Avoid treating per-client Device MFT bypass or application-specific
   sample-routing as an implemented feature. Shared camera transforms
   generally operate on the camera processing path, not reliably on
   caller-specific streams.
6. Keep the current opt-in Device MFT disabled between experiments. Install
   only after reviewing test scope, signed build provenance, and rollback.

Acceptance criteria: (a) Deep Live Cam continually displays a changing
unmodified physical scene, (b) OBS Window Capture visibly receives its
processed output, (c) Chrome selects the existing physical Logi C270 name and
displays OBS content simultaneously, (d) no recursive camera loop, and (e)
stopping OBS/Bridge restores original Logitech video without breaking capture.
