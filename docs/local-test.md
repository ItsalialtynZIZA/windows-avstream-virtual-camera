# Local Windows 11 x64 test plan

Use a development/test machine. Kernel-driver test signing changes the system trust configuration and requires a reboot.

## 1. Prerequisites

- Visual Studio 2022 with Desktop development with C++
- Windows 11 SDK 10.0.26100.x
- WDK 10.0.26100.6584
- Elevated PowerShell

## 2. Validate the toolchain

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\check-dev-environment.ps1
```

## 3. Build

```powershell
.\scripts\build-driver.ps1 -Configuration Release
```

Expected package contents include `AvCam.sys` and `AvCam.inf`.

## 4. Enable test signing for development

Run elevated:

```powershell
.\scripts\enable-test-signing.ps1
```

Reboot Windows after the setting changes.

## 5. Install root-enumerated camera

Run elevated and point the script to the built INF:

```powershell
.\scripts\install-driver.ps1 -InfPath '<path-to-built-AvCam.inf>'
```

## 6. Verify PnP enumeration

```powershell
.\scripts\verify-camera.ps1
```

Expected device:

```text
Class: Camera
FriendlyName: AvCam Virtual Camera
Hardware ID: Root\AvCamVirtualCamera
Status: OK
```

## 7. Streaming test

Open the Windows Camera application and select `AvCam Virtual Camera` if multiple cameras are present.

M2 success criteria:

- the camera can be opened without an error;
- the negotiated mode is 1280x720 RGB32;
- a deterministic synthetic diagnostic pattern is visible;
- repeatedly opening/closing the camera does not crash or hang Windows;
- Device Manager continues reporting the device as healthy.

## 8. Cleanup

```powershell
.\scripts\uninstall-driver.ps1
.\scripts\disable-test-signing.ps1
```

Reboot after disabling test signing.

## Current boundary

The current AVStream project is a Windows camera capture device. It is not USB UVC bus/device emulation and does not claim physical USB VID/PID or USB link characteristics.
