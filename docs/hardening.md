# AvCam hardening and runtime stress plan

This plan is for a dedicated Windows 11 x64 development/test VM or machine. Driver Verifier can intentionally bugcheck Windows when a kernel-driver violation is detected. Keep recovery access and crash dumps available.

## 1. Baseline before Verifier

Install and verify the current test package, then confirm:

```powershell
.\scripts\verify-camera.ps1
.\AvCamFeeder.exe --list-sources
```

Run both producer paths separately:

```powershell
.\AvCamFeeder.exe --source test
.\AvCamFeeder.exe --source obs
```

Before enabling Driver Verifier, confirm Windows Camera can repeatedly open and close `AvCam Virtual Camera` without a crash or hang.

## 2. Check Driver Verifier state

Run elevated:

```powershell
.\scripts\driver-verifier.ps1 -Action Status
```

The project intentionally uses one scoped helper. Do not configure Driver Verifier with `/all` for this test.

## 3. Enable one-boot verification for AvCam.sys only

Run elevated on the dedicated test machine:

```powershell
.\scripts\driver-verifier.ps1 -Action EnableOneBoot -IUnderstandThisCanCrashTheTestMachine
```

Review the printed settings and reboot.

## 4. Stress matrix after reboot

Exercise these scenarios while Driver Verifier is active:

1. Open Windows Camera on `AvCam Virtual Camera`, leave it streaming for several minutes, close it, and repeat.
2. Run `AvCamFeeder.exe --source test`; confirm submitted/capture counters advance.
3. Start and stop the feeder repeatedly while Windows Camera stays open.
4. Run `AvCamFeeder.exe --source obs`; start/stop OBS Virtual Camera and confirm reconnect behavior.
5. Close/reopen Windows Camera while the feeder stays running.
6. Disable/re-enable `AvCam Virtual Camera` in Device Manager and confirm the feeder reconnects rather than terminating.
7. Run a browser/WebRTC consumer after Windows Camera testing and repeat camera open/close cycles.
8. Switch between test and OBS sources across separate feeder launches.

Watch for:

- bugchecks;
- hangs during camera close or device disable;
- pool/rundown failures;
- IRQL violations;
- use-after-free during pin/device teardown;
- monotonically increasing dropped-frame counts under sustained back-pressure;
- transport that fails to recover after device or OBS restart.

## 5. Transport counters

The feeder prints:

```text
submitted=...
replaced=...
dropped=...
capture_reads=...
fallback=...
```

Expected behavior:

- `submitted` grows while the producer is running;
- `capture_reads` grows only when a consumer is actively reading AvCam;
- `replaced`/`dropped` may grow when producer cadence exceeds consumer cadence;
- `fallback` should primarily occur before the first producer frame or while no producer frame is available.

## 6. Crash handling

If Windows bugchecks, preserve the dump before changing the driver. Record the exact scenario that triggered the crash and inspect the dump with WinDbg. Do not suppress Driver Verifier findings by disabling the relevant rule; fix the driver path first.

If the machine cannot boot normally, use recovery/safe mode and reset Driver Verifier:

```powershell
verifier.exe /reset
```

Reboot afterward.

## 7. Reset after the pass

If persistent verifier settings remain, run elevated:

```powershell
.\scripts\driver-verifier.ps1 -Action Reset -IUnderstandThisCanCrashTheTestMachine
```

Then reboot.

## 8. Pass criteria for M5 runtime hardening

M5 runtime verification is complete only when the same current driver build passes the stress matrix without:

- Driver Verifier bugchecks;
- camera open/close hangs;
- device-disable/re-enable failures;
- feeder reconnect failures;
- OBS reconnect failures;
- persistent PnP error state after testing.

Compile-time PREfast passing is necessary but does not replace this runtime pass.
