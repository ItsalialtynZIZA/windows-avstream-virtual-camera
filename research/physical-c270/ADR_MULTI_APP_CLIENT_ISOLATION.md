# Architecture decision: Multi-app capture is solved; app-specific physical-frame substitution is not

Date: 2026-10-22

## Confirmed on owner's Windows host
- Windows 11 Pro 25H2 build 26200.9711.
- Physical Logitech C270 endpoint remains selected.
- Windows camera advanced setting "Allow multiple apps to use camera at the same time" is on.
- Owner reports that unchanged Deep Live Cam Studio and Chrome can now view the original C270 concurrently.
- OBS-to-AvCam IPC had previously passed a live ~30fps two-consumer test.

## Why existing Device MFT does NOT solve Chrome-only replacement
Microsoft's Device MFT design guide describes the transform as a **per-device**
extension, inserted for post-capture processing common to a device's streams:
https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dmft-design

Frame Server shares a device capture stream across clients; it does not offer
a documented client-specific transformation callback in this DMFT:
https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dshow-bridge-implementation-guidance-for-usb-video-class-devices

The current AvCamPhysicalHook.cpp only examines negotiated media type, a queue
pointer, and availability of OBS MJPEG from shared memory. NONE identifies
Chrome versus Deep Live Cam. Multi-app support cannot turn this hook into a
Chrome-only filter. It could create a feedback loop if Deep Live Cam consumes
frames derived from its own processed preview.

## Explicit implementation decision
- Keep Device MFT OBS substitution **OFF** in distributed builds.
- Keep the existing original C270 capture path working.
- Do not add process-name checks to shared camera-frame code. The process
  hosting Device MFT can be Frame Server; its name is not client identity.
- Do not infer which consumer owns a frame by startup timing, PID, capture
  resolution, or MJPEG format without proof from a supported API.
- Do not register the Device MFT on owner's main machine to test unspecified
  client routing.

## Implementable next step, subject to owner's requirement
If the owner explicitly permits a different camera endpoint, use a dedicated
OBS-backed software camera for Chrome while Deep Live Cam keeps native C270.
This is a separate product mode, NOT strict physical-C270 acceptance.

If the same existing USB endpoint remains mandatory, the strict contract is
BLOCKED pending a documented and tested per-client route. Continue only
non-invasive read-only analysis or a separate isolated proof-of-concept. Do
not represent the platform limitation as a solved driver task.

## Success gate (same USB endpoint)
1. Deep Live Cam receives changing original sensor frames while OBS runs.
2. Chrome selects the same actual USB interface; not just same friendly name.
3. Chrome receives changing OBS processed frames only.
4. Stopping OBS restores raw frames for Chrome.
5. No feedback loop or driver instability.
