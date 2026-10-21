# C270 physical multi-app capture breakthrough — 2026-10-22

## Owner-confirmed environment and result

- Host: Windows 11 Pro 25H2, build **26200.9711** (winver screenshot).
- Previously: Windows 11 23H2, build 22631, with missing Multi-app camera UI.
- Settings > Bluetooth & devices > Cameras > **Logi C270 HD WebCam** > Advanced camera settings > **Allow multiple apps to use camera at the same time** was available and OFF after upgrade.
- Owner enabled it, applied changes, and reported **success** after being instructed to test Deep Live Cam Studio with Chrome selecting the physical C270 concurrently.
- This is the first positive owner report of simultaneous physical C270 use by those two unchanged applications.
- It does **not** establish that OBS-processed frames arrive in Chrome; that acceptance criterion remains untested and BLOCKED.

## Safety-critical inspection: current OBS Device MFT

AvCamPhysicalHook.cpp uses a process-global MJPEG queue format gate and a fresh OBS
shared-frame mapping. It has NO verified distinction between Chrome and Deep Live
Cam inside a shared Windows Frame Server pipeline. Once a frame enters the
device-wide transform, substitution may affect the Deep Live Cam input too, and
could create feedback via OBS Window Capture.

For this reason commit 6889907a61e468ea9d0d5e92227501fb2899a50a
introduces a compile-time safety gate: OBS frame replacement is disabled in
normal builds. Only an explicitly experimental build defining
AVCAM_EXPERIMENTAL_UNSCOPED_MFT_REPLACEMENT could opt into the old
device-wide behavior. **Do not enable that define on the owner's main PC** until
the feedback and client isolation story is demonstrated.

## Next true acceptance gate

1. Keep Windows Multi-app camera enabled and device MFT registration OFF.
2. Confirm real camera is simultaneously changing in both applications
   without AvCam.
3. Determine whether any supported per-client interception exists.
   Device-scoped MFT should NOT be treated as per-client just because
   multiple applications can open the camera.
4. Test per-client isolation in a controlled development environment
   before proposing physical-device MFT replacement on the real machine.
5. Require Chrome to use the original **PnP USB interface**, not a SWD /
   ROOT endpoint or friendly-name alias.
6. Explicitly distinguish shared acquisition success from OBS substitution
   success; never conflate the two.

The existing OBS-to-AvCam IPC live fanout result (~30 fps) remains valid as
an independent subsystem test.
