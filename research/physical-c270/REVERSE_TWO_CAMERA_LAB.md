# Reverse two-camera AvCam laboratory

## Goal and identity

Input: original physical Logitech C270 USB webcam (VID_046D PID_0825) selected inside Deep-Live-Cam.
OBS captures the Deep-Live-Cam preview window and publishes its processed output through OBS Virtual Camera.
Output candidate: the original internal USB2.0 HD UVC webcam (VID_2B7E PID_B685) opened by Chrome.

The lab MFT is device-scoped: **any** consumer of the internal camera may see processed OBS frames. Its 1280x720 MJPEG-only hook means support for this internal webcam's negotiated format is UNKNOWN until measured. This experiment does not alter USB VID/PID and cannot establish physical sensor provenance or pass identity/liveness checks.

## Before switching (required)

1. Close Chrome, Deep-Live-Cam, Windows Camera and old AvCam bridge.
2. Using elevated PowerShell with OLD package: `C:\AvCam-Dual-Lab\AvCamDualCameraMft.ps1 -Disable`.
3. Require `AVCAM_DUAL_CAMERA_C270_LAB_DISABLED`; stop on failure and retain original rollback files.
4. Restart Windows before using Logitech as Deep-Live-Cam input, allowing Camera Frame Server to release the previous Logitech Device MFT.
5. Open OBS first; start OBS Virtual Camera; then select **Logi C270 HD WebCam** in Deep-Live-Cam, and use OBS Window Capture. Check the live processing preview.
6. Without any new MFT installation, Chrome can select `OBS Virtual Camera` to test the full video pipeline. Selecting the internal physical USB webcam will show original RAW video until its own supported post-capture transform is installed.

## Optional separate internal-camera lab

After Logitech rollback has succeeded, use a fresh artifact **AvCam-Reverse-Cameras-Lab-x64** with the guarded `AvCamBuiltInCameraMft.ps1` script. Do not mix it with the old Logitech installer. In elevated PowerShell:

```powershell
cd C:\AvCam-Reverse-Lab
.\AvCamBuiltInCameraMft.ps1
.\AvCamBuiltInCameraMft.ps1 -Enable -DualCameraLab
```

Only when the script lists the **built-in** camera and confirms Logitech is clean, enter `ENABLE_BUILTIN_OUTPUT_LAB`. The tool refuses installation if the old Logitech rollback record exists, the source Logitech has any Device MFT, a foreign MFT is registered on the built-in camera, or the lab COM class is already registered elsewhere. It verifies its own dedicated DLL checksum before copying and persists the exact built-in registry path for rollback.

Restart camera clients (and preferably Windows). Start OBS Virtual Camera, Deep-Live-Cam on Logitech, then run `AvCamPhysicalBridge.exe` from an elevated console. For a WebRTC test, choose the **original USB2.0 HD UVC WebCam**, and compare RAW/OBS behavior at 640x480 vs 1280x720. The lab only attempts MJPG 1280x720; anything else passes through. If camera capture fails or hangs, perform rollback instead of installing more drivers.

## Rollback of the reverse lab

```powershell
cd C:\AvCam-Reverse-Lab
.\AvCamBuiltInCameraMft.ps1 -Disable
```

Expected `AVCAM_BUILTIN_OUTPUT_LAB_DISABLED`. The recorded built-in USB interface path is used even when device enumeration fails. After rollback, restart Windows. Do not delete `lab-install-state.json` manually on cleanup errors.

## Verification scope

- Existing confirmed: OBS-to-AvCam MJPEG 90 frames @ ~30fps on second laptop in the original two-camera arrangement.
- To verify separately: physical Logitech RAW as Deep-Live-Cam input, Windows Media Foundation pipeline on the internal USB webcam, exact camera media formats, Chrome video output, OBS stop/recovery, clean reverse rollback.
- An installed Device MFT is NOT proof that Chrome selected a physical device or received synthetic frames. The OS camera stack may negotiate unsupported types.
