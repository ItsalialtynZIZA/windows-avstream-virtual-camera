# Windows 11 Multi-app Camera compatibility gate (physical C270)

Status: documented Windows feature; NOT YET VALIDATED on owner's C270.

## Why this gate exists

Microsoft documents **Allow multiple apps to use camera at the same time**
in Windows 11 version 24H2+:
https://support.microsoft.com/en-us/windows/hardware/camera/manage-cameras-with-camera-settings-in-windows-11

This is materially different from DShow Bridge's UVC registry opt-in:
that earlier test on the owner's C270 did not fix Chrome's busy-camera error
and was rolled back. Do NOT repeat EnableDshowRedirection as a substitute.

The previously captured Chrome MF_E_HW_MFT_FAILED_START_STREAMING
(0xC00D3704) is consistent with a blocked camera stream, but its precise
cause has not been proved. The feature above should be checked before
designing a new low-level replacement driver.

## Read-only discovery (safe on main Windows)

Run in PowerShell (does not enable or disable multi-app mode):

    [Environment]::OSVersion.Version
    Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber
    Get-PnpDevice -Class Camera | Where-Object InstanceId -Match 'VID_046D&PID_0825' | Format-List FriendlyName,Status,InstanceId

Then OPEN (do not change any switches yet):
Settings > Bluetooth & devices > Cameras > Logi C270 HD WebCam >
Advanced camera options / Edit.

Record only:
- Windows version and build number
- Whether Multi-app camera / Allow multiple apps to use camera at the same time is shown
- Whether it is currently enabled or disabled
- Whether the switch is available or greyed out

Windows support page explicitly notes this menu applies to Windows 11 24H2+
and may be controlled by an organization. The exact layout is version-dependent.
Avoid assuming it exists merely because BuildNumber >= 26100.

## Separate acceptance gates

Gate 1: WITHOUT AvCam Device MFT and Bridge, changing only the supported
Windows multi-app setting (after the owner's informed opt-in) permits BOTH:
(a) unchanged Deep Live Cam Studio processing changing C270 video;
(b) Chrome opening the EXISTING physical C270 endpoint simultaneously.
If this gate fails, stop. Do not install/re-register AvCam Device MFT.

Gate 2: Only IF Gate 1 succeeds, controlled opt-in Device MFT OBS substitution
and Bridge are tested for Chrome, while original Deep Live Cam input remains
stable. Beware: a shared Device MFT may affect BOTH clients or produce a
recursive loop. Never claim per-client transformation without proof.

Gate 3: When OBS stops, original raw camera frames are restored; no camera
loop, no driver regression, and original physical C270 device selected.

If Gate 1 cannot be established for this actual device/OS, strict endpoint
contract remains BLOCKED. A separate virtual software camera is only an
explicitly accepted change to the requirement, not success under Gate 1.

## Source pointers
- https://support.microsoft.com/en-us/windows/hardware/camera/manage-cameras-with-camera-settings-in-windows-11
- https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dshow-bridge-implementation-guidance-for-usb-video-class-devices
- https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source
- C270_ETW_2026-10-21_FINDINGS.md
