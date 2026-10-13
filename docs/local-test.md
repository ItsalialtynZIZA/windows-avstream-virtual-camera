# Local Windows 11 x64 test plan

Use a dedicated development/test machine. Kernel-driver test signing changes the Windows trust configuration and requires a reboot. Do not use this procedure on a production machine.

## 1. Preferred test inputs: GitHub Actions artifacts

From the latest successful `Validate` run for `dev/initial-avstream`, download and extract:

- `AvCam-test-package-x64`
- `AvCamFeeder-x64`

The driver artifact contains the test-signed driver package plus installation/verification helper scripts. The feeder artifact contains `AvCamFeeder.exe`.

## 2. Test-signing state

The helper installer does not enable test-signing automatically.

If the test machine is already configured for test driver development, confirm its state first. If test-signing must be enabled, use the repository helper from an elevated PowerShell and reboot:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\enable-test-signing.ps1
```

This changes the machine's driver trust configuration. Use it only on a dedicated test machine.

## 3. Install the CI test package

Open PowerShell as Administrator in the extracted driver artifact directory.

First try the guarded installer without changing certificate stores:

```powershell
.\scripts\install-test-package.ps1 -PackageDirectory .
```

If Windows rejects the package because the CI test certificate is not trusted, and this is a dedicated test machine, explicitly opt in to certificate trust:

```powershell
.\scripts\install-test-package.ps1 -PackageDirectory . -TrustTestCertificate
```

The script does not enable test-signing by itself.

## 4. Verify PnP enumeration

Run elevated:

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

Also confirm the device appears in Device Manager under `Cameras`.

## 5. M2 synthetic-stream test

Before starting the feeder, open Windows Camera and select `AvCam Virtual Camera` if multiple cameras are present.

Expected fallback behavior:

- camera opens without an error;
- mode is 1280x720 RGB32;
- deterministic synthetic diagnostic image is visible;
- repeatedly opening/closing the camera does not crash or hang Windows.

## 6. M3 user-mode transport test

Start the extracted feeder executable in a normal console:

```powershell
.\AvCamFeeder.exe
```

Expected startup sequence includes messages similar to:

```text
AvCam feeder
AvCam producer interface detected / AVStream filter accepted
Streaming 1280x720 BGRA32 test frames
```

The image in Windows Camera should switch from the driver's static synthetic fallback to the feeder's moving test pattern.

Approximately once per second the feeder should print transport counters:

```text
stats: submitted=... replaced=... capture_reads=... fallback=...
```

Interpretation:

- `submitted` increasing: user-mode -> driver transport works;
- `replaced` increasing: producer is replacing the latest buffered frame as designed;
- `capture_reads` increasing while Windows Camera is open: AVStream capture pin is consuming submitted frames;
- `fallback` increasing before the first submitted frame: synthetic fallback path is active.

Stop the feeder with `Ctrl+C`.

## 7. Reconnect test

With the feeder running, disable/re-enable or restart the AvCam device during development testing.

Expected feeder behavior:

- it reports the transport loss;
- it does not terminate;
- it waits for AvCam to reappear;
- it reopens the matching camera filter and resumes test-frame submission.

## 8. Browser enumeration test

After Windows Camera works, test Chrome or Edge on a normal camera-permission test page or a local WebRTC test page.

Success criteria:

- `AvCam Virtual Camera` is offered as a camera source;
- selecting it produces video;
- starting/stopping browser capture does not destabilize the driver.

## 9. Cleanup

Run elevated:

```powershell
.\scripts\uninstall-driver.ps1
```

If test-signing was enabled specifically for this test machine and is no longer required:

```powershell
.\scripts\disable-test-signing.ps1
```

Reboot after changing test-signing state.

## Current boundary

This project creates a Windows AVStream camera capture device. It is not USB UVC bus/device emulation and does not claim physical USB VID/PID, USB link speed, or other hardware-bus characteristics.
