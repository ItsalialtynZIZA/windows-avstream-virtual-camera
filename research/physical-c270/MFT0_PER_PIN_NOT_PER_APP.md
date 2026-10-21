# MFT0 is per camera stream, not proven per consumer (2026-10-22)

## Microsoft references

- [Considerations for Driver MFTs on multi-pin cameras](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/driver-mfts-on-multi-pin-cameras)
  explicitly states that a separate MFT0 instance is attached to each
  *pin* on a capture source. It also explains how preview and capture
  can fan out from a single capture pin. A pin is NOT the same thing as
  a Chrome client session.
- [Creating a camera driver MFT](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/creating-a-camera-driver-mft)
  describes a per-stream camera MFT registered for a device.
- [Windows 11 advanced camera options](https://support.microsoft.com/en-us/windows/hardware/camera/manage-cameras-with-camera-settings-in-windows-11)
  describes Multi-app camera as sharing the stream among multiple
  unchanged applications. It does not document app-specific frame
  overrides or an authenticated app-identity callback.

## Consequence for strict AvCam acceptance

The owner's physical C270 now works simultaneously with unchanged Deep
Live Cam and Chrome on Windows 11 25H2. That solves the hardware-opening
problem but not selective OBS substitution.

Do not implement OBS frame replacement in MFT0 based on:
- being a separate instance;
- a negotiated camera pin or MJPG subtype;
- dimensions, frame rate or startup order;
- a camera process ID (could be Frame Server).

None of those proves the frame belongs only to Chrome. An unscoped
MFT0 could still change input for Deep Live Cam and produce feedback.

## Engineering gate

Before a prototype modifies the device's MFT0 registration, establish
a supported way for unchanged Chrome's getUserMedia session to opt in
to an OBS-backed per-client transform while Deep Live Cam stays raw.
Without this, strict physical-endpoint Chrome-only output remains
BLOCKED. Do not change the physical camera registry/driver in an
attempt to assume an application-specific effect.

## Validated components

- Original USB Logitech C270, Windows 11 25H2 multi-app sharing: PASS
  reported by device owner.
- OBS -> AvCam IPC, two readers about 30 fps: PASS previously reported.
- Chrome receives OBS frames while still selecting physical C270:
  NOT DEMONSTRATED.
