# AvCam second-PC dual-camera red-scene trial (2026-10-26)

## Observed second-PC baseline

- Windows 11 Education x64 build 26200; Ryzen 7 7735HS; 15.2 GiB RAM; NVIDIA RTX 4050 Laptop GPU used by OBS.
- Independent webcam inputs present: built-in `USB\VID_2B7E&PID_B685` and physical Logitech C270 `USB\VID_046D&PID_0825`.
- User confirmed OBS Virtual Camera works when launched before Deep Live Cam Studio.
- User confirmed working AvCam RAW vs OBS preview on this PC.
- `AvCamFeeder.exe --source avcam-ipc-obs --check-source --seconds 3` returned `frames=90`, `changed=29`, exit code 0. Thus OBS samples are available in IPC V2.
- `--check-virtual-camera` and `--check-transport` both returned ABSENT / exit code 6. No AvCam AVStream test driver is installed.

## Different from the original one-camera proposal

This experiment uses the **built-in camera for Deep Live Cam input**, reserving the distinct physical Logitech C270 as the candidate output capture endpoint. It removes the known direct feedback loop that would result if Deep Live Cam itself consumed processed C270 samples. It does **not** prove that Chrome can accept arbitrary Device MFT output through the unmodified physical C270 interface.

Microsoft's Device MFT is a *device-scoped, post-capture processing extension*. If activated, the effect may apply to **all applications using C270**, not only Chrome. Device-wide replacement has a wider blast radius, including Windows Camera and other applications. A tested IPC stream alone is not evidence that a registered MFT is loaded or compatible with C270's negotiated capture type. In particular, the current sample hook only replaces **MJPG 1280x720** in its older experimental branch.

## Stage R0 — read-only baseline (implemented)

1. Leave both stock camera drivers unchanged. Keep experimental Device MFT unregistered.
2. From a standard PowerShell session, download `AvCamDualCameraRedPreflight.ps1` from this branch, and run:
   ```powershell
   .\AvCamDualCameraRedPreflight.ps1
   ```
3. Confirm `AVCAM_DUAL_CAMERA_PREFLIGHT=PASS`, healthy PnP devices, and `AVCAM_LOGITECH_MFT_STATE=ABSENT`. Anything else is a stop sign.
4. Optionally, while the AvCam Router and OBS are running:
   ```powershell
   .\AvCamDualCameraRedPreflight.ps1 -CheckObsIpc -FeederPath C:\AvCam-Second\AvCamFeeder.exe
   ```
5. `-SelfTest` tests the device-identity selection with synthetic data, without enumerating real devices.

This script **never** installs a driver, registers a COM class, writes registry keys, accesses camera video, or changes firmware/boot security. Its PnP checks do not verify the actual Deep Live Cam camera selection; confirm that manually in the application. Registry lookups may yield UNKNOWN on a restricted account; UNKNOWN fails closed.

## Stage R1 — red source, without touching C270 (ready for user test)

1. In OBS, while the existing scene is working, create a separate *test scene* with a solid-red Color Source. This scene is a diagnostic, not a face image.
2. Start OBS Virtual Camera **before** Deep Live Cam if both are needed, matching this host's successful order.
3. Keep the existing AvCam Router open and inspect the right **OBS** pane via `AvCam-Start.cmd -Mode compare`.
4. Confirm the OBS pane shows red. Verify the left **RAW Logitech** pane remains live and unchanged.
5. Restore the previous OBS scene and verify OBS IPC resumes. This validates only the application-space routing and fallback, **not** MFT insertion or Chrome C270 output.

## Stage R1b — deterministic red MJPEG transport (implemented in C++)

The new `AvCamPhysicalBridge.exe --red-test` emits 90 valid 1280x720 red JPEG frames over approximately three seconds. It requires neither OBS nor any camera device. Unlike the normal publisher, the red test uses an **isolated** `Local\\AvCamRedMjpegDiagnosticV1` mapping and a separate named mutex. No installed physical C270 Device MFT can subscribe to that diagnostic name. It never starts a camera capture, modifies the physical Logitech driver, registers COM, or alters browser settings.

From the matching *new CI artifact* containing `AvCamPhysicalBridge.exe`:

```powershell
.\AvCamPhysicalBridge.exe --red-test
```

Expected line: `AVCAM_RED_MJPEG_TRANSPORT_PASS frames=90 ... camera_modified=0`. If OBS IPC is working and the physical Device MFT is absent, this test establishes only the JPEG frame publication path, **not** that Chrome receives synthetic red through the original physical C270 endpoint. CI executes this red publisher on camera-free Windows runners.

## Stage R2 — native physical C270 transform (blocked pending separate implementation/review)

The existing `AvCamPhysicalInstall.ps1 -Enable` remains deliberately disabled, and `AvCamMftBuildSafety.h` rejects `AVCAM_EXPERIMENTAL_UNSCOPED_MFT_REPLACEMENT` at compile time. Do **not** remove those guards or invoke obsolete RC3 install scripts merely because the input cameras are now physically separate.

Before considering a new, controlled device-wide experiment:

- Establish the exact C270 media types and the Windows Camera / Chrome Frame Server path without modifying it.
- Implement and review a **new** reversible registration and rollback mechanism with an explicit operator confirmation, integrity check, an exclusive device whitelist, and refusal if any unrelated MFT is registered.
- Independently verify that the Microsoft sample Device MFT preserves original capture on all error, timeout, and shutdown paths. A Windows Frame Server hang was previously observed with an *unrelated experimental software-camera* activation and must not be confused with a successful MFT test.
- Obtain an isolated Windows test/recovery plan; keep actual Windows driver and application work local to the user's authorized test PC.
- Do not mislabel synthetic or processed media as unaltered physical sensor data or rely on altered video for identity/liveness verification.

**Status:** R0 script committed; R1 application-space red scene can be tested. No physical Logitech C270 pixel substitution is implemented or claimed by this change.
