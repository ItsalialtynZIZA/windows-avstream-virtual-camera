# Two-camera OBS -> ORIGINAL C270 Device MFT laboratory

**Experimental / opt-in only. Not validated by Chrome yet.**

The host has two *different* physical cameras:
- built-in `USB2.0 HD UVC WebCam` (`VID_2B7E PID_B685`) is the input selected in Deep-Live-Cam;
- external physical `Logi C270 HD WebCam` (`VID_046D PID_0825`) is the camera selected in Chrome.

OBS Virtual Camera ingests the Deep-Live-Cam window capture. AvCamPhysicalBridge publishes fresh MJPEG to a LocalService-readable `Global\\AvCamPhysicalMjpegV1` mapping. The **dedicated lab Device MFT** is bound to the original C270 camera device interface and attempts to replace C270's **MJPG 1280×720** samples.

The actual USB camera hardware is unchanged; this is an application-facing Media Foundation pixel transform, **not** a USB write to the sensor or a trustworthy indication that the footage originated from the physical lens. **Every MF consumer of C270 may see the processed video**; the lab does not isolate Chrome. Do not use it for identity/liveness claims.

## Verified earlier on second Windows 11 laptop

- Both physical webcams enumerate successfully in Windows.
- OBS + Deep-Live-Cam works with OBS Virtual Camera started *before* Deep-Live-Cam.
- `AvCamPhysicalBridge.exe --obs-test`: 90 frames, 30 changed sample hashes, 30.48 FPS, success.
- Before installation, `CameraDeviceMftClsid` and `CameraDeviceMftClsidChain` absent on physical C270, and the lab CLSID not registered.

**None of those checks validates a loaded C270 Device MFT or proves Chrome displays OBS.**

## Build artifact

Download **`AvCam-Dual-Camera-Lab-x64`** from successful `.github/workflows/avcam-physical-c270.yml` run on `dev/manual-vhci`. It contains:
- `AvCamDualCameraDeviceMft.dll`: explicitly enabled device-wide laboratory transform;
- `AvCamDualCameraMft.ps1`: status/guarded enable/reversible disable;
- `AvCamPhysicalBridge.exe`: existing OBS-to-JPEG publication process;
- `LAB_SHA256SUMS.txt`: required lab DLL checksum.

The default `AvCamPhysicalDeviceMft.dll` build remains pass-through, and the legacy `AvCamPhysicalInstall.ps1 -Enable` remains disabled. No test-signed kernel driver, Secure Boot change, WDK, custom USB descriptor, OBS extension, or renamed camera is needed for this experiment.

## Explicit laboratory procedure (authorized, recoverable second laptop only)

1. Keep this project's install-state backup intact. Close Chrome / Windows Camera / AvCam preview. Verify Deep-Live-Cam uses **built-in**, not Logitech, as source.
2. Extract the artifact into `C:\AvCam-Dual-Lab`.
3. From **64-bit Administrator PowerShell** run:

   ```powershell
   cd C:\AvCam-Dual-Lab
   .\AvCamDualCameraMft.ps1
   .\AvCamDualCameraMft.ps1 -Enable -DualCameraLab
   ```

4. Read the warning and type the *exact requested confirmation phrase* only if this is an intentional test. The script checks the two independent PnP cameras, refuses unrelated transforms/COM registration, verifies checksum and backs up the C270 binding before changing it.
5. Restart the camera applications; if Windows keeps an old Camera Frame Server instance, an OS restart may be necessary. Launch OBS, start OBS Virtual Camera, then launch Deep-Live-Cam and verify the preview is from the **internal camera**. Keep OBS scene showing its changing window capture.
6. From another **elevated** PowerShell in this directory start:

   ```powershell
   .\AvCamPhysicalBridge.exe
   ```

   The publisher reports `OBS Virtual Camera is actively publishing frames to AvCam.` when it has fresh frames. Leave it running. The bridge does **not** capture physical Logitech.
7. Open Chrome, choose the existing **`Logi C270 HD WebCam`** camera in a legitimate camera test page, and verify video. If the device appears but shows RAW or black video, do not label the test successful: check media-type negotiation and FrameServer/MFT ETW events first. The hook only processes MJPG 1280×720 in its lab branch; other formats pass through.
8. Exit Chrome, terminate the bridge with `Ctrl+C`, and verify returning physical frames. For full deregistration, follow rollback below.

## Rollback

From elevated PowerShell:

```powershell
cd C:\AvCam-Dual-Lab
.\AvCamDualCameraMft.ps1 -Disable
```

The rollback reads its saved exact physical Logitech interface path and removes only its own `CameraDeviceMftClsid`. It does not require a functioning/connected Logitech. Unregistration leaves the recovery record in place if COM cleanup fails; **do not delete it manually**. Close/reopen camera clients, or reboot if the Frame Server still has the prior DLL loaded. The bridge alone can be stopped with `Ctrl+C`, after which stale JPEG expires and the transform should pass through Logitech RAW.

## Exit criteria

Mark `CHROME_PHYSICAL_C270_OBS_PASS` only if (1) Chrome itself selects the original physical C270, (2) its pixels visibly track changes in OBS, (3) Deep-Live-Cam is still using the independent built-in camera, (4) stopping OBS/bridge falls back to RAW without hang, and (5) rollback restores stock behavior.

CI compiles and checks this experimental build without connecting to hardware, registering COM, or starting a Chrome camera. Hardware acceptance is not proven until run on the owner's test laptop.
