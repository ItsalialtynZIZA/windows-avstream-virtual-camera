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


## 2026-10-29: Actual native MJPEG type confirmed; physical capture next

Owner ran the verified, non-installing `AvCamIntegratedCameraFormats.exe --inspect` on Windows 11 build 26300, Lenovo 82K1. `AVCAM_INTEGRATED_MJPEG_1280X720=ADVERTISED`: single Integrated Camera source, 27 native types; `MJPG 1280x720 @30/1` and `NV12 1280x720 @30/1` are both advertised. No AvCam DLL was attached to Sonix. `AVCAM_MFT_CHAIN_INTEROP=NOT_VERIFIED`.

The additional `AvCamIntegratedNativeCaptureSmoke.exe` validates actual 30 native JPEG samples from the OEM **real camera** pipeline at 1280x720@30; the wrapper `Run-AvCamIntegratedNativeCaptureSmoke.ps1` stops only its test process after 30 seconds. It saves no frames or identifiable images and does not enable AvCam processing. Run only when Windows Camera, OBS capture, Chrome video tabs, and Deep-Live-Cam are closed. From the current `AvCam-Integrated-13D3-Lab-x64` artifact extracted to `C:\AvCam-Integrated-Lab`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\AvCam-Integrated-Lab\Run-AvCamIntegratedNativeCaptureSmoke.ps1"
```

A successful run prints `AVCAM_CAPTURE_NEGOTIATED=MJPG_1280X720_30`, `AVCAM_INTEGRATED_NATIVE_MJPEG_CAPTURE_PASS`, and `AVCAM_BOUNDED_NATIVE_CAPTURE_COMPLETE`. This checks normal genuine video from the OEM Sonix chain, not interoperability with a second MFT or the Chrome-selected type. Windows CI only compiles/selftests without opening a camera.


## 2026-10-29: physical MJPEG baseline passed; wrapper exit-code fixed

Owner's Lenovo 82K1, Windows 11 build 26300, with original Sonix OEM chain (unchanged):

- `AVCAM_CAPTURE_CAMERA_MATCHES=1`
- `AVCAM_CAPTURE_NEGOTIATED=MJPG_1280X720_30`
- `AVCAM_CAPTURE_FRAMES=30 CHANGED_PREFIXES=29 MALFORMED=0 TICKS=1 NON_INCREASING_TS=0 ELAPSED_MS=1484 FLAGS=256`
- `AVCAM_INTEGRATED_NATIVE_MJPEG_CAPTURE_PASS`
- `AVCAM_MFT_CHAIN_INTEROP=NOT_VERIFIED`

The old 5.1 PowerShell wrapper printed `AVCAM_NATIVE_CAPTURE_FAILED exitcode=` **after** native success, because its `Start-Process -PassThru` exit-code value was empty. The wrapper now uses a directly-owned .NET Process with synchronous exit observation, async output draining, and no registry writes. `-WrapperSelfTest` checks exit code 0 and 17 without camera, and the user confirmed `AVCAM_BOUNDED_WRAPPER_SELFTEST_PASS`. No hardware re-test is necessary solely to confirm the old wrapper error.

The new `AvCamSonixChainPlan.ps1` is a **read-only, standalone planner** for the observed `REG_MULTI_SZ` chain `[Sonix]`. It calculates the candidate `[Sonix, AvCam]`, preserving Sonix nearest DevProxy, plus an exact original-chain rollback requirement. It intentionally rejects nonmatching OEM values, duplicates, old Windows builds and incompatible chain shapes. No `-Enable` option exists; it never installs COM DLLs or edits the camera. `AVCAM_SONIX_CHAIN_INSTALL=BLOCKED` remains the decision until the pin counts, required COM interfaces, media-type negotiation, source Frame Server behavior and rollback are actually verified. Microsoft's documented chain-capacity support is not proof of interoperability.

No frame-authenticity or biometric/liveness compatibility claims are made.


## 2026-10-29: transactional chain registration foundation, no OEM changes

The sandbox-only `AvCamSonixChainTransaction.ps1` implements and verifies the critical install/restore transaction flow without touching the camera or system registry. It **does not offer any production install switch**.

The test creates one uniquely named `HKEY_CURRENT_USER\Software\AvCam\SonixChainTransactionTest\<GUID>` key, initializes a simulated OEM `REG_MULTI_SZ` list, writes a durable JSON record of the exact original list before installing the simulated `[Sonix, AvCam]` list, and restores the original only if the current value still equals the expected simulated candidate. It also checks idempotent restore, recovery after injected post-write failure, refusal to overwrite a concurrently modified chain, and rejection of an unexpected registry value type. The isolated test key and temporary JSON records are removed at the end.

This adds real rollback logic and guards to the codebase, but **does not** prove that the Sonix DLL and AvCam DLL agree on pin topology or can initialize together under Windows Camera Frame Server. Current real-device installation remains blocked. A future reviewed installer must add production-specific backups, a crash recovery path, code-signature validation, accurate device paths, signed COM registration, and native hardware acceptance before any camera change. No user action required on Lenovo for this milestone.


## 2026-10-29: real upstream Device MFT count telemetry in AvCam sample

The adapted Microsoft sample Device MFT now calls `AvCamTraceDeviceMftTopology` from its `InitializeTransform` cleanup path. The original sample calls `MF_DEVICEMFT_CONNECTED_FILTER_KSCONTROL` / `IMFTransform::GetStreamCount` to discover the upstream transformation's output pins and then builds AvCam's own pin arrays. The ETW event (provider `{A56F32CA-31E5-4C6D-95F2-74DB9DA141E9}`) reports only HRESULT, upstream input/output counts, AvCam input/output counts and a preliminary count-match indicator; it does not contain frames or app identities.

**Do not infer OEM pin counts from the single Media Foundation presentation stream** reported by `AvCamIntegratedCameraFormats.exe`. The predecessor in `InitializeTransform` might be DevProxy or another DMFT, so the event explicitly says `upstream_identity=unknown`, `pin_types=unknown` and `chain_interop=NOT_VERIFIED`. A count match alone never authorizes real camera installation. The ETW event will only be produced if AvCam is instantiated by an actual test pipeline: current read-only OEM diagnostics do not load or install AvCam, so they cannot generate an OEM-Sonix topology event. All existing OEM-chain fail-closed installation gates remain in place.

CI verifies the upstream count logging hook is staged exactly once in the adapted reference MFT and builds the DLL without registering it. Next work: safely establish the identity and output pin topology of the upstream OEM transform and perform verified format/stream-ID matching in an isolated compatible lab, before considering system-wide camera changes.


## 2026-10-29: Connected upstream media-type telemetry (not a Lenovo install)

A further adaptation of the pinned Microsoft Device MFT reference now performs **bounded, read-only enumeration** of the connected predecessor's `IMFDeviceTransform::GetOutputAvailableType` after receiving its actual upstream output stream IDs. The patch is applied by `Adapt-Sample.ps1`; its implementation is in `AvCamPhysicalHook.cpp`. For each upstream output pin it records the numeric stream ID, number of available media types inspected (maximum 64), whether enumeration reached `MF_E_NO_MORE_TYPES`, and whether MJPG or NV12 at 1280x720@30 were advertised. No frame content, PnP identity, application identity, or raw biometric data is recorded.

The ETW decoder `AvCamPhysicalCaptureEtw.ps1` now reports `AVCAM_UPSTREAM_MEDIA_CONTRACT_EVENTS_OBSERVED`, `AVCAM_UPSTREAM_MEDIA_MJPG_720P30_PINS` and `AVCAM_UPSTREAM_MEDIA_OEM_IDENTITY=UNKNOWN`. CI statically verifies the hook is staged once and that the type-query bound and not-verified marker remain.

**Evidence boundary:** these events can only appear when an AvCam Device MFT actually receives `InitializeTransform` from an upstream camera pipeline. The current owner's Lenovo **has not had AvCam appended to its Sonix OEM chain**. Thus no new upstream data has been obtained from the Lenovo and no compatibility or working Chrome output can be claimed. Native camera MJPG capture evidence is not equivalent to connected Sonix→AvCam type negotiation. The OEM chain and physical camera remain untouched; no installer enabling this path is included. A separately scoped test host and reviewed rollback are required for any subsequent live chain experiment.

The physically separate `OBS Virtual Camera` remains a usable non-invasive selection in supported apps. Replacing the original OEM camera endpoint for all consumers is not a proof of genuine sensor provenance or camera-origin authenticity.

## 2026-10-29: Simultaneous user-space baseline on Lenovo, BEFORE any Sonix chain installation

The owner confirmed the current `AvCamPhysicalBridge.exe --obs-test` output:
`AVCAM_OBS_MJPEG_LIVE_PASS frames=90 changed=22 fps=30.15 camera_modified=0`.
While Logitech -> Deep-Live-Cam -> OBS were active, Chrome selected the
**original** Lenovo Integrated Camera and displayed its **original sensor
image**, not the OBS feed.

These are two distinct pieces of evidence: OBS frames arrive at an isolated
AvCam diagnostic transport, and the unchanged Sonix camera remains available
to Chrome. They do NOT show a link between that OBS feed and the OEM camera
endpoint. In `--obs-test`, AvCam uses the private
`Local\\AvCamObsMjpegDiagnosticV1` mapping: it is intentionally not the
original camera's production output.

`AvCamDmftMediaContract.h` now also contains a **portable synthetic**
adjacent-MFT MJPEG-format gate. It requires explicit, verified pin mapping
and two connected, fully enumerated format descriptors, accepts equivalent
30-fps rational rates, and rejects NV12 for the current MJPEG-only path.
It does NOT infer OEM Sonix identity or approve installation. The original
camera's `CameraDeviceMftClsidChain` stays unchanged.

## 2026-10-29: Isolated adjacent Device MFT pin-link simulation

The portable `AvCamDmftConnectedPinMap.h` and `AvCamDmftConnectedPinMapTest.cpp`
model one-to-one predecessor-output to successor-input links with **opaque,
nonidentical stream IDs** and validate their completeness before the existing
MJPEG 1280x720@30 media contract is considered. Duplicate links, unknown IDs,
duplicate source IDs, oversized counts, missing links and missing evidence are
all rejected without allocating unbounded memory.

The test runs on Linux and Windows CI using synthetic arrays only. It does
**not** instantiate SonixDeviceMFT.dll, connect the actual OEM transform to
AvCam, install any COM class, or establish live Lenovo camera compatibility.
Its purpose is to prevent a later chain adapter from assuming consecutive or
identical stream IDs. Physical Sonix installation remains blocked.

The owner also confirmed Chrome displays the **original** Integrated Camera
while Deep-Live-Cam and OBS run: this is normal RAW pass-through and not
evidence that the OEM camera is fed from OBS. An isolated test and a safe
reversible hardware evaluation would be required before any OEM modification.

## 2026-10-29: Optional signed Sonix COM contract inspection, no installation

`AvCamSonixComContract.exe` is included in the **Integrated-13D3 lab artifact**
as a narrowly scoped diagnostic. Windows CI runs its **`--selftest` only**,
with no Sonix DLL on the runner.

The optional on-device inspection requires the exact Windows system directory
module `System32\SONiX\SonixDeviceMFT.dll`, a locally valid Authenticode
signature, and an explicit `--i-accept-oem-dll-execution` flag. It uses the
observed Sonix CLSID `{5A50829A-86DD-4D18-8685-891EEE643C24}` and
direct `DllGetClassObject` / `IClassFactory` / `QueryInterface` calls in the
diagnostic process, without registering or appending any COM class to the
camera interface. It **does load and execute OEM code** (DllMain and class
constructor), so this is NOT strictly read-only with respect to arbitrary
OEM DLL runtime side effects. Run it only as a separately consented
experiment in a disposable process with an external timeout; do not assume
no effects just because the AvCam diagnostic makes no registry writes.
It deliberately does **not** call `InitializeTransform`, enumerate video
pin media types, acquire frames or open the camera.

The test reports support for `IMFDeviceTransform`, `IKsControl`,
`IMFRealTimeClientEx` and `IMFShutdown` interfaces, plus the mandatory
`AVCAM_SONIX_CONNECTED_PINS=NOT_VERIFIED` and
`AVCAM_SONIX_CHAIN_INSTALL=BLOCKED` markers. A PASS only proves
*class factory instantiation and relevant QueryInterface support*; it does
not prove pin mapping, negotiated MJPEG frames, or compatibility with AvCam.

No production installation is supported. The `AvCamIntegrated13D3Mft.ps1`
`-Enable` mode remains forbidden on the observed OEM Sonix chain.

## 2026-10-29: confirmed device-wide OBS output (two distinct physical cameras)

**Accepted product requirement:** the owner explicitly accepts processed OBS
frames being shown to every application opening the original Lenovo
`Integrated Camera` (USB VID_13D3&PID_56FF). This is device-scoped,
not Chrome-scoped. Deep-Live-Cam must take its RAW input from the separate
Logitech C270 (VID_046D&PID_0825), never from the Lenovo output.

Target: Logitech C270 RAW -> Deep-Live-Cam -> OBS Virtual Camera ->
AvCamPhysicalBridge -> **Lenovo Sonix Device MFT -> AvCam Device MFT** ->
every Media Foundation consumer of Lenovo Integrated Camera.

The camera name / hardware device identity remains Lenovo's own, but
the processed pixels come from OBS when the format matches and the publisher
is live. This does not make processed footage originate from the physical
Lenovo sensor, and does not guarantee compatibility with authenticity or
liveness checks. The replacement must fail open to the real Lenovo frame
on stale/invalid/absent OBS data or unsupported media type.

**Completed on the real host:** Sonix-only OEM MFT chain enumerated and
Authenticode signature valid; Sonix required COM interfaces are available;
the original Lenovo MF source captured 30 genuine MJPG 1280x720@30 frames
(29 changed, 0 malformed). OBS-to-AvCam diagnostic has independently
received moving frames. This is NOT a successful Sonix->AvCam chain.

**Engineering blockers before a device-wide on-device trial:**

1. Verify actual adjacent Sonix output pin IDs and type enumeration against
   AvCam input pins after the real OEM transform has been initialized.
   Matching the camera's MF source descriptor is not sufficient.
2. Implement chain-aware registration using the existing REG_MULTI_SZ
   `CameraDeviceMftClsidChain`, keeping Sonix CLSID first and appending
   only AvCam; never write the legacy `CameraDeviceMftClsid` value.
3. Add exact original-chain snapshot, digest/validation, crash-safe
   recovery and an independent rollback path that does not require a
   working camera. Deny install when the OEM baseline has changed.
4. Verify 1280x720 MJPG timing and all-consumer output with moving OBS
   content, then stop the bridge and verify safe physical pass-through.
5. Confirm that Deep-Live-Cam continues capturing Logitech RAW, that
   no OBS feedback loop exists, and that Chrome plus Windows Camera work
   without FrameServer hangs before approving any release.

**Current permission:** consent to device-wide OUTPUT semantics, not a
blanket instruction to edit the host's OEM camera registry. The current
chain registration remains BLOCKED until interoperability and recovery
are validated and a separate explicit hardware-trial decision is made.

`AvCamSonixChainPlan.ps1` is a read-only candidate planner only; it now
requires distinct healthy physical RAW input and processed output cameras.

## Prior real-hardware OBS replacement achieved on physical Logitech (2026-10-20)

On the *previous laptop*, the owner reported that both **Windows Camera**
and **Chrome**, with the **original physical `Logi C270 HD WebCam`** selected,
displayed the red OBS test scene. This confirms that AvCam's Device MFT
replacement mechanism demonstrably worked for the Logitech physical endpoint
in those applications. This is more than CI compilation or an IPC smoke test.

The evidence has an important limitation: the Chrome/Windows Camera
OBS-pixel success was observed **without Deep-Live-Cam running**. When
Deep-Live-Cam used that same Logitech as RAW input simultaneously with
Chrome, the user encountered occupied-camera/stream instability.
Therefore the previous laptop did NOT pass the full concurrent
Logitech RAW -> Deep-Live-Cam -> OBS -> Logitech physical endpoint
end-to-end acceptance.

**What to reuse, not reinvent:** the existing `AVCAM_DUAL_CAMERA_LAB_MFT`
branch of `AvCamPhysicalHook.cpp` already accepts fresh OBS MJPEG
1280x720 frames through `AvCamPhysicalBridge` and can replace negotiated
MJPG samples device-wide. The new host must feed Deep-Live-Cam from the
external Logitech and apply that existing proven frame-replacement approach
to the *different* Lenovo Integrated Camera output after its pre-existing
Sonix Device MFT. Only the Sonix->AvCam chain integration is new.

The new Lenovo has an existing `CameraDeviceMftClsidChain` containing Sonix.
Current old `AvCamIntegrated13D3Mft.ps1` single-CLSID installer cannot
safely append to that OEM chain and remains blocked. Real Sonix COM QI
and the camera's native 30-frame MJPG capture already passed, but **actual
adjacent Sonix->AvCam pin mapping, negotiated format, device-wide OBS
replacement and rollback have not been demonstrated**. Do not claim otherwise.
