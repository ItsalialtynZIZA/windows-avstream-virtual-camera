# Windows 11 Frame Server hang during experimental AvCam MF capture — 2026-10-22

## Observed on actual Windows laptop (user-supplied WER records)

- System build: 26200 (Windows reported "Windows 10 Pro" product label).
- RAW Logitech C270 and processed OBS ran concurrently at ~30 FPS in
  AvCamRouterPreview with 0 reconnects.
- Session-0 LocalService read 3 live processed NV12 frames via the IPC pipe.
- HKLM AvCam-owned COM CLSID was registered; MFCreateVirtualCamera and
  IMFVirtualCamera::Start returned success; Camera and Chrome enumerated
  the separate AvCam IPC Video endpoint.
- Attempting live capture was followed by failure of multiple camera apps.
- 2026-10-22 20:13:06 local: Application Error ID 1000 for
  svchost.exe_FrameServer, ntdll.dll, code 0xCFFFFFFF.
- 2026-10-22 20:13:12: Windows Error Reporting ID 1001,
  APPCRASH label, same FrameServer PID, code 0xCFFFFFFF.
- 2026-10-22 20:17:42: Windows Error Reporting ID 1001,
  AppHangTransient for DeepLiveCamStudio.exe.

Microsoft defines 0xCFFFFFFF as STATUS_APPLICATION_HANG, **not**
STATUS_ACCESS_VIOLATION. The WER APPCRASH label alone does not establish
memory corruption. Do not attribute a fault to ntdll.dll or any specific
AvCam line without debugging the FrameServer dump/wait chains.

These data strongly associate the hang with the new virtual-camera
capture path but do not prove precise root cause.

## Safety actions

1. Stop temporary AvCam camera session and preview, stop Camera/Chrome,
   OBS and Deep Live Cam, then reboot the Windows host.
2. Remove ONLY the owner-verified AvCam experimental COM registration
   using Manage-AvCamMfSource.ps1 -Action Unregister -AcceptExperimental
   in elevated x64 PowerShell. Never alter Logitech PnP drivers.
3. Confirm registration absent. Files may remain locked until reboot;
   do not forcibly stop FrameServer, take ownership of system files,
   or delete third-party COM entries.
4. Verify the physical Logitech camera and Deep Live Cam standalone.
5. Preserve the WER event report; any FrameServer minidump may contain
   sensitive process memory/video data and should be handled privately.
6. DO NOT attempt --session in the experimental host again until a
   reviewed fix plus stress/cleanup validation exists.

## Technical findings and hypotheses (not proven cause)

- Prior stream implementation permitted concurrent IMFMediaSource::Start/
  Stop/Shutdown and IMFMediaStream2::SetStreamState() access to a shared
  std::thread without a lifecycle mutex. Such races can cause an MF hang.
- The prior SetStreamState implementation called Start/Stop which also
  queued MEStreamStarted/MEStreamStopped events; the official Windows
  virtual-camera sample explicitly avoids emitting those events on
  state transitions requested via IMFMediaStream2.
- The prior media stream stored its parent media source as an unowned raw
  pointer even though FrameServer can hold an IMFMediaStream reference
  after the activation/source object lifecycle changes.
- A single OBS pipe client and a bounded 1200ms payload read can introduce
  Stop/Shutdown delays, especially when the writer has disappeared.
  Analyze a dump before declaring that as the cause.

## Changes on dev/manual-vhci

- Serialize stream worker lifetime transitions and safely retain a parent
  IMFMediaSource reference, released at Shutdown.
- Avoid duplicate MEStreamStarted/MEStreamStopped during SetStreamState.
- Update CI smoke tests for extra events and source lifecycle.
- Require explicit --session --accept-unstable-frame-server on new builds:
  plain --session fails closed with
  AVCAM_SOFTWARE_CAMERA_FRAMESERVER_HANG_QUARANTINED.
  This is a guardrail, **not** a declaration that live capture is safe.

## Exit criteria for lifting the quarantine

- Windows CI compile and COM/stream/event tests green on latest commit.
- Multi-threaded Start/Stop/Shutdown/SetStreamState lifetime stress test,
  including writer disconnect/reconnect, is green.
- WER dump triage resolves the previous FrameServer hang or identifies
  a supported alternative Media Foundation architecture.
- Recovery test demonstrates no FrameServer hang, no Deep Live Cam loss,
  and clean complete removal from an isolated designated test machine.
- User expressly authorizes another on-device risk after receiving the
  findings. Never silently re-enable legacy live capture.
