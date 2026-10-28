# Local Windows 11 x64 test plan

Use a dedicated development/test machine. Kernel-driver test signing and Driver Verifier can change system behavior and can require a reboot. Do not use this procedure on a production machine.

## 1. Preferred test input: one GitHub Actions bundle

From the latest successful `Validate` run for `dev/initial-avstream`, download and extract:

- `AvCam-runtime-test-x64`

The combined artifact contains:

```text
driver\
  driver\x64\Release\AvCam\AvCam.inf
  driver\x64\Release\AvCam\AvCam.sys
  driver\x64\Release\AvCam\avcam.cat
  driver\x64\Release\AvCam.cer
  scripts\...
  docs\local-test.md
  docs\hardening.md
feeder\
  AvCamFeeder.exe
```

Open an **elevated PowerShell** in the extracted `AvCam-runtime-test-x64` directory and define these paths once:

```powershell
$DriverDir = (Resolve-Path .\driver).Path
$Feeder = (Resolve-Path .\feeder\AvCamFeeder.exe).Path
Set-ExecutionPolicy -Scope Process Bypass
```

The separate `AvCam-test-package-x64` and `AvCamFeeder-x64` artifacts remain available for debugging, but the combined runtime bundle is preferred for normal testing.

## 2. Test-signing state

The helper installer does not enable test-signing automatically.

If test-signing must be enabled, use the guarded helper from elevated PowerShell:

```powershell
& "$DriverDir\scripts\enable-test-signing.ps1" -IUnderstandThisChangesBootTrust
```

Reboot Windows after the change. This changes Windows boot/driver trust policy. The helper deliberately does not disable Secure Boot or alter firmware settings. If platform policy blocks test-signing, use a dedicated driver-development test environment instead of weakening firmware security automatically.

After reboot, return to the extracted runtime-bundle directory and re-create `$DriverDir` and `$Feeder` if this is a new PowerShell session.

## 3. Install the CI test package

**This driver is test-signed.** The installer now refuses to create a device
unless test-signing is already confirmed active in the current boot and the
operator explicitly opts in. If `bcdedit /enum '{current}'` has no
`testsigning Yes` (or localized `Да`), stop here. Do not weaken Secure
Boot on a primary workstation to make this lab work.

On a dedicated, previously prepared test machine use an elevated PowerShell:

```powershell
& "$DriverDir\scripts\install-test-package.ps1" -PackageDirectory $DriverDir -AcceptTestSignedDriver
```

If the signed package is rejected only because its test certificate is not
trusted, review the actual signer and opt in explicitly on that test machine:

```powershell
& "$DriverDir\scripts\install-test-package.ps1" -PackageDirectory $DriverDir -AcceptTestSignedDriver -TrustTestCertificate
```

The current CI artifact includes `AvCamRootInstall.exe`, which uses
SetupAPI/NewDev to register *only* `Root\AvCamVirtualCamera`. **DevCon
and the Windows Driver Kit are not required** on the test host. The helper
does not enable test-signing, disable Secure Boot, register a Sonix Device
MFT, or modify a physical USB camera.

To undo installation, run the included `scripts\uninstall-driver.ps1`
as administrator. It uses the same bundled native tool for the separate
AvCam root device; it does not require DevCon.

## 4. Verify PnP enumeration

Run elevated:

```powershell
& "$DriverDir\scripts\verify-camera.ps1"
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
- mode is 1280x720 top-down RGB32;
- deterministic synthetic diagnostic image is visible;
- capture is paced by the driver's ~30 fps timer rather than by consumer-buffer arrival rate;
- repeatedly opening/closing the camera does not crash or hang Windows.

## 6. M3 user-mode transport test

Start the feeder with the deterministic test source:

```powershell
& $Feeder --source test
```

Expected startup sequence includes messages similar to:

```text
AvCam feeder
Frame source started: moving-test-pattern
Accepted by AVStream filter: ...
Streaming 1280x720 BGRA32 frames
```

The image in Windows Camera should switch from the driver's static synthetic fallback to the feeder's moving test pattern.

Approximately once per second the feeder should print transport counters:

```text
stats: submitted=... replaced=... dropped=... capture_reads=... fallback=...
```

Interpretation:

- `submitted` increasing: user-mode -> driver transport works;
- `replaced` increasing: producer is replacing the latest buffered frame as designed;
- `dropped` increasing: bounded frame slots are applying back-pressure rather than allocating indefinitely;
- `capture_reads` increasing while Windows Camera is open: AVStream capture pin is consuming submitted frames;
- `fallback` increasing before the first submitted frame: synthetic fallback path is active.

Stop the feeder with `Ctrl+C`.

## 7. Reconnect test

With the feeder running, disable/re-enable or restart the AvCam device during development testing.

Expected feeder behavior:

- it reports the transport loss;
- it does not terminate;
- it waits for AvCam to reappear;
- it reopens the matching camera filter and resumes frame submission.

## 8. Inspect Media Foundation camera names

Before the OBS test, list the names Windows exposes through Media Foundation:

```powershell
& $Feeder --list-sources
```

Confirm that an OBS camera source is present. A normal result should include a name similar to:

```text
OBS Virtual Camera
```

If the name is different, use a distinctive case-insensitive substring with `--device-name` in the next step.

## 9. M4 OBS Virtual Camera bridge test

Start OBS and configure the scene that should be forwarded. Then start `OBS Virtual Camera` in OBS.

Run:

```powershell
& $Feeder --source obs
```

If the installed OBS build exposes a different friendly name, specify a case-insensitive substring:

```powershell
& $Feeder --source obs --device-name "OBS Virtual Camera"
```

Expected behavior:

1. the feeder finds the OBS capture device through Media Foundation;
2. Source Reader requests RGB32 1280x720 @ 30 fps using advanced video processing;
3. Media Foundation color conversion/resize/frame-rate conversion is used when necessary;
4. RGB32 rows are copied using the actual `IMF2DBuffer` pitch, or the negotiated signed `MF_MT_DEFAULT_STRIDE` fallback, into the top-down AvCam transport layout;
5. output media type is revalidated if Source Reader reports `CURRENTMEDIATYPECHANGED`;
6. frames are sent through the private KS transport using the corrected method-data directions;
7. Windows Camera showing `AvCam Virtual Camera` displays the OBS scene;
8. `submitted` and `capture_reads` counters continue increasing.

If OBS Virtual Camera is not running when the feeder starts, the feeder should wait and retry rather than terminate. If OBS Virtual Camera disappears while streaming, the source is stopped and re-enumerated until it becomes available again.

## 10. Browser enumeration test

After Windows Camera works, test Chrome or Edge on a normal camera-permission test page or a local WebRTC test page.

Success criteria:

- `AvCam Virtual Camera` is offered as a camera source;
- selecting it produces video;
- when the feeder uses `--source obs`, the browser receives the OBS scene through AvCam;
- starting/stopping browser capture does not destabilize the driver.

## 11. M5 Driver Verifier test — dedicated test VM/machine only

Driver Verifier intentionally stresses kernel drivers and can bugcheck Windows when it detects a violation. Microsoft recommends running it only on machines used for driver testing/debugging. Do not run this on a production or important workstation.

Check current verifier state first from elevated PowerShell:

```powershell
& "$DriverDir\scripts\driver-verifier.ps1" -Action Status
```

After all normal M2-M4 tests above work, configure **AvCam.sys only** for one boot:

```powershell
& "$DriverDir\scripts\driver-verifier.ps1" -Action EnableOneBoot -IUnderstandThisCanCrashTheTestMachine
```

Review `verifier /querysettings`, then reboot the dedicated test machine. After reboot, exercise:

- Windows Camera open/close repeatedly;
- `$Feeder --source test`;
- `$Feeder --source obs`;
- OBS Virtual Camera start/stop;
- AvCam device disable/enable;
- browser camera open/close;
- feeder/consumer reconnect loops.

If Driver Verifier detects a kernel violation, Windows may bugcheck. Preserve the crash dump and analyze it with WinDbg before changing the driver.

To clear persistent Driver Verifier settings explicitly:

```powershell
& "$DriverDir\scripts\driver-verifier.ps1" -Action Reset -IUnderstandThisCanCrashTheTestMachine
```

Then reboot. The reset action clears persistent Driver Verifier settings on that test machine, not only AvCam-specific settings.

## 12. Cleanup

Run elevated:

```powershell
& "$DriverDir\scripts\uninstall-driver.ps1"
```

If test-signing was enabled specifically for this test machine and is no longer required:

```powershell
& "$DriverDir\scripts\disable-test-signing.ps1"
```

Reboot after changing test-signing state.

## Current boundary

This project creates a Windows AVStream camera capture device. It is not USB UVC bus/device emulation and does not claim physical USB VID/PID, USB link speed, or other hardware-bus characteristics.
