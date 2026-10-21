# AvCam strict C270 endpoint: post-fanout research (2026-10-22)

## Acceptance
Unmodified Chrome selects the original physical Logitech C270 VID_046D PID_0825 while unchanged Deep Live Cam takes original raw capture. Chrome should receive OBS-processed frames, with no browser extension, separate virtual camera, global feedback loop or endpoint rename.

## Proven on owner PC
- Windows 11 25H2 Multi-app sharing: Chrome and Deep Live Cam simultaneously access physical C270.
- OBS to AvCam IPC: two readers at approximately 30 fps.
- Historical Device MFT red-scene substitution only with Deep Live Cam closed.
- Current MFT is unregistered. Current build prohibits unscoped replacement.

## Evaluated Windows APIs
1. Device MFT is a per-device processing component, not a documented Chrome-specific post-fanout effect.
   https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dmft-design
2. MediaCapture.AddVideoEffectAsync is called by the capture application to attach effects to its own MediaCapture instance. Unmodified Chrome does not opt into a custom effect via this API.
   https://learn.microsoft.com/en-us/windows/apps/develop/camera/effects-for-video-capture
   https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/custom-video-effects
3. Frame Server Custom Media Source requires a driver package and registered camera interface. That creates/replaces a camera source; it does not document post-fanout routing for unchanged Chrome on the existing C270.
   https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source
4. MFT0 per camera pin does not establish per-app capture identity.
   https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/driver-mfts-on-multi-pin-cameras

## Decision
No supported API identified among the mechanisms examined that meets all strict requirements. State BLOCKED, not a proof of universal impossibility. Never infer per-client isolation from FrameServer PID, queue pointer, negotiated subtype, capture pin, dimensions, startup order or OBS IPC success.

## Next engineering gate
Require a documented or directly validated post-fanout transform that has an authenticated Chrome-specific consumer signal **without changing Chrome or Deep Live Cam**. Until then do not register a shared-camera replacement DLL or claim a successful strict acceptance test.
