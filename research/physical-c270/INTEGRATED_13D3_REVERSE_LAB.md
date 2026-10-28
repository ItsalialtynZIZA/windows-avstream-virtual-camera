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


## Observed baseline on new laptop: existing Sonix Device MFT chain

User ran the October 28, 2026 artifact `AvCam-Integrated-13D3-Lab-x64`:

```
AVCAM_OBS_MJPEG_LIVE_PASS frames=90 changed=22 fps=30.16 camera_modified=0
Healthy Integrated Camera (VID_13D3 PID_56FF) devices: 1
Existing CameraDeviceMftClsid:
Existing CameraDeviceMftClsidChain: {5A50829A-86DD-4D18-8685-891EEE643C24}
PREFLIGHT_ONLY: original Integrated Camera USB device/registry untouched.
```

**DO NOT call `-Enable -DualCameraLab` for this laptop with the current installer.** The existing `CameraDeviceMftClsidChain` is a manufacturer/provider processing chain, and the installer deliberately fails closed on any nonempty chain. According to Microsoft's UVC documentation, when the chain exists the *legacy* `CameraDeviceMftClsid` entry is ignored, so writing it would not correctly append our transform even if we relaxed the guard: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-camera-implementation-guide

The existing CLSID `{5A50829A-86DD-4D18-8685-891EEE643C24}` has been reported as the `SonixDeviceMFT.dll` COM class in a public security advisory: https://herolab.usd.de/en/security-advisories/usd-2023-0029/ . This is evidence of *likely Sonix processing*, **not** proof that the owner's current driver is vulnerable. Read the actual COM `InprocServer32` path, code signature, and registry ACL without changing them; if permissions are unsafe, prefer verified OEM/Windows Update remediation. Do not delete or rewrite OEM chain/registry/DLL.

Future support for this notebook requires a separately designed **chain-aware**, reversible installer that preserves the exact original `REG_MULTI_SZ` chain, adds its own Device MFT only after compatible format/capture evidence, verifies appropriate ordering, enforces the OS chain length, and restores the exact original chain on rollback. Tests must prove correct interoperability with the OEM DMFT, real Chrome frame capture, and failure/rollback. The existing `AvCamIntegrated13D3Mft.ps1` is **not** chain-aware; do not use `-Enable` until a purpose-built implementation is reviewed. Selecting `OBS Virtual Camera` directly in Chrome remains the no-registry-change fallback.


## 2026-10-29: Lenovo 82K1 camera OEM chain and ACL hardening

- Hardware model confirmed: Lenovo IdeaPad Gaming 3 15IHU6 (82K1).
- Sonix Integrated Camera driver: `10.13.22621.14`, provider Sonix, INF `oem144.inf`, 2023-09-13.
- OEM COM class `{5A50829A-86DD-4D18-8685-891EEE643C24}` resolves to `C:\Windows\system32\SONiX\SonixDeviceMFT.dll` with a valid Microsoft Windows Hardware Compatibility Publisher signature.
- The Windows 11 camera interface already contains the Sonix `CameraDeviceMftClsidChain`; no AvCam MFT was installed into this chain.
- The owner saved the original Sonix COM registry values and discretionary ACLs to a dated local backup. Both `HKLM\SOFTWARE\Classes\CLSID\{5A50829A-86DD-4D18-8685-891EEE643C24}` and its `InprocServer32` had explicit `Allow / FullControl` ACEs for built-in Users (S-1-5-32-545).
- The owner ran the targeted ACL hardening command successfully: `BACKUP_VERIFIED`, two `ACL_FIXED` markers, `SONIX_ACL_HARDENING_COMPLETE`. Follow-up read-only inspection showed only inherited `ReadKey` and `GENERIC_READ` for Users on both keys. This confirms the *identified explicit Users write ACEs* were removed; it is not a complete effective-access audit of all principals or inherited permissions.
- Functionality of Windows Camera **after** hardening has not yet been reported; verify RAW capture before any later camera work.

### Next safe build milestone: read-only OEM chain preflight

`AvCamSonixChainPreflight.ps1` is a new diagnostic tool (not a driver or installer). It checks the single connected 13D3:56FF Integrated Camera; resolves the `VIDEO_CAMERA` interface by exact USB instance; checks the chain registry type and CLSID list; reads COM DLL locations/signatures and Sonix COM registry ACLs. It does **not** register COM, modify camera settings, write ACLs, or attach AvCam to the Sonix chain.

To run from a local checkout of `dev/manual-vhci` in **64-bit Windows PowerShell**, use:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\research\physical-c270\AvCamSonixChainPreflight.ps1
```

`-SelfTest` exercises only syntactic chain validation and write-rights mask classification, without a connected camera. The existing `AvCamIntegrated13D3Mft.ps1` remains intentionally blocked for OEM chains; no supported chain-aware installation has been implemented or hardware validated yet. A read-only preflight result must never be labeled end-to-end camera compatibility or proof of origin/authenticity.
