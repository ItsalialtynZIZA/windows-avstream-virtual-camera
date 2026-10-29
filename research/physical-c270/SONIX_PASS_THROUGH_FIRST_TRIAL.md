# Lenovo Integrated Camera: Sonix → AvCam first hardware trial

**Purpose:** answer whether the existing Lenovo Integrated Camera (USB VID_13D3&PID_56FF, Windows 11 build >=22621) can start with an **additional, non-replacing AvCam Device MFT** after the signed Sonix OEM transform.

This trial **DOES NOT output OBS into Integrated Camera**. It does not modify the USB driver and it does not claim that OBS is original sensor video. A successful Chrome preview establishes a real live chained-MFT test, which is needed before any OBS frame-replacement experiment.

## Prerequisites

- OBS → separate root AvCam works independently; physical Logitech C270 remains the RAW input for Deep Live Cam.
- Integrated Camera shows genuine sensor images in Chrome before trial.
- Download the CI artifact `AvCam-Integrated-13D3-Lab-x64` from the `AvCam Physical Logitech C270 / OBS Device MFT (experimental)` workflow; extract into `C:\AvCam-Integrated-Lab`.
- Close Chrome, Windows Camera, Deep Live Cam, OBS, and any other camera readers. Disconnect unnecessary camera apps.
- Have access to an elevated PowerShell **even if the camera stops working**.

## Step A: read-only assessment

```powershell
cd C:\AvCam-Integrated-Lab
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamSonixChainPreflight.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamSonixChainTrial.ps1 -Action Status
```

Expected exactly one Sonix CLSID `{5A50829A-86DD-4D18-8685-891EEE643C24}`, and no existing rollback record. Any mismatch: **stop**. The pass-through DLL's SHA-256 and COM-interface contract are separately validated by the installer **before** registry mutation.

## Step B: explicit optional hardware trial

This **changes the physical camera's OEM Device MFT chain** (not the hardware/USB driver) and can make Integrated Camera unavailable until rollback and reboot. Never run unattended.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamSonixChainTrial.ps1 -Action Install -AcceptHardwareRisk
```

Typed confirmation: `INSTALL_AVCAM_PASSTHROUGH_ONLY`. This mode copies `AvCamPhysicalDeviceMft.dll`, **not** `AvCamDualCameraDeviceMft.dll`. Default Device MFT hook returns original samples without injecting OBS data. Installer checks exact camera PnP identity, independent Logitech RAW source, OEM Sonix signature, single registry key, original one-element chain, COM CLSID availability, payload hash and COM contract, then records an on-disk rollback manifest **before** touching the OEM chain.

Reboot Windows after successful installation (not merely restarting Chrome).

## Step C: real connected topology acceptance

1. Open elevated PowerShell and start:
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamPhysicalCaptureEtw.ps1 -Target IntegratedChrome -Seconds 35
```
2. While tracing, select **Integrated Camera** in Chrome's ordinary `getUserMedia` camera test and observe a genuine, moving sensor image, not OBS.
3. Share the ETW summary: look for `AVCAM_DMFT_TOPOLOGY_EVENTS_OBSERVED`, connected upstream counts, upstream media types, and `AVCAM_MFT_REPLACEMENT_EVENTS_OBSERVED=0`.
4. The ETW parser is observational only. Camera enumeration, COM QI and upstream pin count alone do **not** prove Sonix→AvCam type/link compatibility. An absence of events is inconclusive.

**Do not activate the OBS-replacing MFT** just because the pass-through trial works. A separate connected pin/type assessment and rollback verification are required before device-wide OBS output.

## Emergency rollback — even if Chrome/Windows Camera are black

From elevated PowerShell, **without opening any camera app**:
```powershell
cd C:\AvCam-Integrated-Lab
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamSonixChainTrial.ps1 -Action Rollback
Restart-Computer
```

Rollback restores the original Sonix-only `REG_MULTI_SZ` chain using the saved exact OEM registry path, then unregisters **only** AvCam's lab COM DLL. It must not delete the state file unless restoration verifies. If rollback fails, preserve `%ProgramFiles%\AvCamSonixChainTrial\oem-chain-rollback.json` and report the exact error.

## Exit criteria

- OEM Sonix-only baseline is preserved in rollback snapshot.
- No unrelated USB, OEM DLL or Logitech registry changes.
- Chrome can open the **original Integrated Camera** and shows real moving frames after the pass-through trial.
- AvCam ETW reports connected MFT topology and enumerated media types with no OBS replacement.
- Rollback brings the physical camera back to its original behavior after restart.

None of the above substitutes OBS into the original camera yet. It proves whether a supported next phase is technically viable on this hardware.
