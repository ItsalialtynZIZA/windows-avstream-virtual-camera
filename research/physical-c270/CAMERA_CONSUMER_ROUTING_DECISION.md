# AvCam — camera consumer routing decision (2026-10-22)

## Verified on the owner's Windows 11 machine

- Logitech C270 physical RAW and OBS processed input both reached `AvCamRouterPhysicalRawObs.exe`: 30/30 frames in the short test.
- Continuous mode reached 1803 RAW / 1803 OBS frames in approximately one minute with no reconnect.
- Starting without OBS, losing OBS, and reattaching OBS succeeded in the read-only user-space router. RAW continued while OBS was absent.
- **None of these tests made the physical Logitech C270 endpoint output OBS to Chrome.**

## Platform constraints — supported sources

1. Windows 11 **Multi-app camera** permits several applications to access the same camera stream **without modifications**. It does not provide an app-specific replacement feed: https://support.microsoft.com/en-us/windows/hardware/camera/manage-cameras-with-camera-settings-in-windows-11
2. Microsoft describes Device MFT (DMFT) as a **per-device** transform common to the streams, after capture. DMFT is not evidence of a distinct browser-consumer route: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dmft-design
3. The documented `MFCreateVirtualCamera` registers a **separate virtual camera** based on a custom media source. The OS distinguishes this endpoint from a physical camera. This is supported for processed-feed delivery, but does not satisfy the requirement for unchanged Chrome selection of the **original physical Logitech USB camera**: https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera
4. Microsoft's reference virtual-camera implementation exposes a separate camera, optionally wrapping a physical camera. It does **not** demonstrate per-application frames on an unchanged physical endpoint: https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera

## Scope decision

- **Shippable and tested:** two independent in-process RAW / processed feeds, RAW isolation, OBS reconnect, and optional consumer-controlled processed output.
- **Supported Chrome integration alternative:** user explicitly selects a separate virtual camera or uses an application-owned capture pathway. Must not be described as the physical C270.
- **Blocked requirement:** unchanged Chrome selecting the original Logitech C270 endpoint receives OBS while an unchanged Deep Live Cam simultaneously receives physical RAW. **No documented, demonstrated consumer-specific interception mechanism has been found.** Do not promise this feature, install a global Device MFT, register COM hooks or alter kernel camera drivers on that basis.
- **Safety:** keep physical device identity truthful; no browser/device fingerprint spoofing or bypassing liveness, identity verification, or access controls.

## Engineering next steps

1. Productize the already verified user-mode Router: clean shutdown, liveness watchdog, observability, and independent channel health.
2. If the user approves a **distinct camera endpoint**, implement a supported Media Foundation virtual-camera source based on Microsoft's sample, keeping the real Logitech intact and ensuring no RAW-to-OBS feedback loop.
3. Preserve the original physical-device requirement as **unresolved**, with an explicit supported-mechanism proof gate in `AvCamPerClientRoutingPolicy.h`.
