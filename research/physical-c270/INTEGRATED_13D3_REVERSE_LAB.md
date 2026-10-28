# AvCam reverse-camera test — Windows 11 Pro build 26200, Integrated Camera 13D3:56FF

## Observed on the user's new laptop (2026-10-28)

- Windows 11 Pro x64, build `26200`.
- Source **Logi C270 HD WebCam**, PnP `USB\VID_046D&PID_0825&MI_00\6&241E5F6C&0&0000`, status OK.
- Potential browser output **Integrated Camera**, PnP `USB\VID_13D3&PID_56FF&MI_00\6&4154320&0&0000`, status OK.
- Previous test folders `C:\AvCam-Dual-Lab` and `C:\AvCam-Reverse-Lab` absent.
- OBS was not found at the default Program Files path; this alone does not prove OBS is not installed (portable/custom install possible). Deep-Live-Cam was not running; this does not prove it is missing.

**The previous `AvCamBuiltInCameraMft.ps1` is for `VID_2B7E&PID_B685` only and must not be installed on this notebook.**

## Pipeline

Logitech C270 → Deep-Live-Cam → OBS Window Capture → OBS Virtual Camera → AvCamPhysicalBridge → optional *device-wide* Device MFT attached to original `Integrated Camera` → Chrome.

The USB identity remains genuinely attached to the physical Integrated Camera. This is a Windows user-mode video post-processor, not sensor-origin video or a guarantee that authenticity checks will accept footage. It affects all camera consumers that use the transformed pipeline, not only Chrome.

## Before any system change

1. Confirm Logitech RAW works in Deep-Live-Cam, and OBS successfully captures Deep-Live-Cam preview.
2. Run OBS Virtual Camera, run the included `AvCamPhysicalBridge.exe --obs-test` to verify OBS frames are published/encoded, and confirm `AVCAM_OBS_MJPEG_LIVE_PASS`.
3. Test raw Integrated Camera in a standard WebRTC resolution test at VGA and HD/720p. Not every camera advertises MJPEG 1280x720, which is the **only** format the existing MFT can replace. A blank or unsupported HD mode is NOT a reason to register the MFT.
4. If the output device's supported formats are unknown, avoid enabling the MFT until there is evidence of compatible capture. Registration success alone does not imply working frames.

## Reversible experiment

Download `AvCam-Integrated-13D3-Lab-x64` from the successful Windows build job on `dev/manual-vhci`, and extract to `C:\AvCam-Integrated-Lab`.

Read-only preflight in 64-bit PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\AvCam-Integrated-Lab\AvCamIntegrated13D3Mft.ps1"
```

The install explicitly requires healthy `VID_13D3&PID_56FF&MI_00` named `Integrated Camera`, healthy Logitech C270 with **no camera MFT**, no foreign MFT or post-processing plugin on the output, no old Logitech lab rollback record, no conflicting COM class, a correct package checksum, administrator privileges and a typed confirmation. It stores the exact registry interface path for recovery.

If and only if baseline RAW and OBS diagnostics both passed, optionally install using elevated 64-bit PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\AvCam-Integrated-Lab\AvCamIntegrated13D3Mft.ps1" -Enable -DualCameraLab
```

At the prompt type `ENABLE_INTEGRATED_13D3_LAB`. Success marker: `AVCAM_INTEGRATED_13D3_LAB_ENABLED`. Restart Windows or at least restart all camera consumers and FrameServer. Start OBS Virtual Camera, start Deep-Live-Cam using **Logitech**, start `AvCamPhysicalBridge.exe` elevated, then open Chrome and choose the original **Integrated Camera**. Test actual frames: VGA 640x480 generally passes through; MJPEG 1280x720 replacement requires observed matching capture negotiation.

## Rollback

Before stopping bridge, stop Chrome and Deep-Live-Cam, then run (admin):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\AvCam-Integrated-Lab\AvCamIntegrated13D3Mft.ps1" -Disable
```

Success marker: `AVCAM_INTEGRATED_13D3_LAB_DISABLED`. Restart Windows. Do **not** manually delete the script's installation-state record on a partial failure; it is needed for safe cleanup.

## Evidence level

CI: C++ compilation, PowerShell syntax and fail-closed install gate; NO camera registration and NO Chrome camera testing. Hardware acceptance requires live tests performed on the actual laptop. This must not be used as evidence for identity verification/liveness claims.
