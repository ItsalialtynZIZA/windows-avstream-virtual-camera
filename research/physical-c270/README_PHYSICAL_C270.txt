AvCam Physical C270 - Windows 11 25H2 status (2026-10-22)
=========================================================

IMPORTANT: EXPERIMENTAL DEVICE MFT INSTALLATION IS DISABLED

The old RC3 one-click installation instructions are obsolete. Do NOT
run AvCamSetup.cmd, AvCamPhysicalInstall.ps1 -Enable, or
AvCamPhysicalControl.ps1 -Action Start / -Action Setup.
These operations are intentionally blocked in current source.

OWNER-CONFIRMED WORKING BASELINE
- Physical USB Logitech C270 (VID_046D, PID_0825).
- Windows 11 Pro 25H2, build 26200.9711.
- Windows multi-app camera mode enabled.
- Deep Live Cam Studio and Chrome simultaneously access the original C270.
- OBS-to-AvCam shared frame IPC separately tested at about 30 FPS.

NOT YET WORKING
- Chrome receives OBS-processed frames through the EXISTING C270 USB
  interface while Deep Live Cam continues to receive raw frames.
- Device MFT does not have proven per-client frame isolation.
  Device-wide OBS replacement risks recursive face-processing feedback.

SAFE ACTIONS
- Keep Windows multi-app camera mode enabled.
- Leave experimental Device MFT unregistered.
- The current manager allows Status, Stop, RemoveAutostart and Uninstall
  for old installations. Old downloaded RC3 scripts lack newer guards.
- Keep install-state.json until old registration has been rolled back.

BUILD POLICY
- AvCamMftBuildSafety.h forbids the unscoped OBS-replacement preprocessor
  definition.
- Adapt-Sample.ps1 stages that header for Windows Device MFT build.
- Updated Windows CI outcome is not yet confirmed.

RESEARCH RECORDS
- MULTI_APP_25H2_OWNER_RESULT.md
- ADR_MULTI_APP_CLIENT_ISOLATION.md
- WINDOWS_MULTI_APP_CAMERA_GATE.md
- C270_ETW_2026-10-21_FINDINGS.md

A virtual SWD/ROOT camera or a renamed endpoint does not satisfy the
strict physical-C270 requirement.
