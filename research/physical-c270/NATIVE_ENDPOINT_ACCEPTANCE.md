# Strict native AvCam requirement (2026-10-22)

## User contract — no Chrome extension

1. Physical Logitech C270 remains the device chosen by unchanged Deep Live Cam and Chrome.
2. Deep Live Cam gets original RAW sensor frames.
3. OBS processes frames; Chrome gets processed OBS output *while still opening the original Logitech device*.
4. No browser extension, JavaScript replacement, manually selecting separate AvCam device, or changing physical camera INF.
5. Stable, reversible, no Windows Camera Frame Server hangs.

Important: a Chrome-visible "Logitech" *name*, the original physical USB device path/instance, and the actual video track source are different technical properties. Do not claim a separate software/emulated UVC camera is the same physical device.

## Verified and not verified

- User confirmed stable Logitech RAW + independent OBS frames in AvCam IPC at about 30fps.
- User confirmed Chrome OBS route via MAIN-world extension; this is **not accepted** as final product.
- User's Windows Camera Frame Server hung with code 0xCFFFFFFF after attempting capture from the separate experimental MF software camera. Do not repeat on the primary laptop.
- We have *not* demonstrated any supported Windows camera pipeline API that routes pixels per consumer (unchanged Chrome vs unchanged Deep Live Cam) while maintaining the same physical Logitech endpoint.
- Userland Device MFT, which is attached per device/pin, is not proven per-client and can risk feeding OBS frames back to Deep Live Cam.
- UdeCx creates an emulated USB host/device with its own device instance, not the original physical Logitech PnP path.

## Sources

Microsoft Device MFT chain in UVC pipeline:
https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-camera-implementation-guide

Device MFT per pin:
https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/driver-mfts-on-multi-pin-cameras

MFCreateVirtualCamera creates a separate labeled virtual software camera:
https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera

UdeCx emulates a new USB device and host controller:
https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-emulated-device--ude--architecture
https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/writing-a-ude-client-driver

## Next native milestones

1. Quarantine and unregister the failed experimental AvCamMF source from the real Windows host. Do not replace the physical C270 driver.
2. Read-only system-level feasibility analysis: enumerate the real USB/PnP symbolic device paths, capture pipeline methods and supported isolation controls. Identify a *documented per-client routing capability* before modifying any physical camera pipeline.
3. Continue kernel UdeCx UVC work as a separately tracked fallback: usbvideo.sys enumeration, MF samples, OBS feeder, safe unplug. Explicitly acknowledge that emulated USB device is **not** the same physical Logitech endpoint and by itself fails strict acceptance.
4. Strict-acceptance gate: show Chrome accesses the same original Logitech physical device instance/path, gets OBS pixels, and Deep Live Cam concurrently gets RAW, without extension/hook. Until demonstrated, requirement is BLOCKED, not "done".
5. Require isolated native-driver testing, cleanup, reboot and non-fatal crash recovery before any renewed attempt on primary laptop.

## Never do

- Do not ship the Chrome extension as the project's final camera solution.
- Do not assume a global camera MFT can isolate processed pixels from Deep Live Cam.
- Do not use process injection or deceive app-level physical-camera authenticity tests.
- Do not retry the unstable MF software camera on the user's main PC.
- Do not modify main; target dev/manual-vhci only.

## Native done criteria

Physical device instance/path unchanged; Deep Live Cam RAW unaffected, Chrome processed OBS under that exact physical instance in unchanged client; repeated start/stop and rollback without any FrameServer hang; demonstrated on actual host, not only CI.
