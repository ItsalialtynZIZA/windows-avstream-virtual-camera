AvCam Physical C270 / OBS - release candidate
================================================

SCOPE
Working on the owner's Logitech C270: Windows Camera selects the real
USB Logitech VID_046D&PID_0825 while showing an OBS test scene.
The Device MFT replaces eligible 1280x720 MJPEG samples in supported
Windows Media Foundation pipelines. The actual Logitech firmware, USB
device descriptors, usbvideo.sys driver, and physical video stream are
unchanged. Browser and other app support is not established by this test.

ONE-TIME SETUP
1. Extract all files from the GitHub Actions artifact into one folder.
2. Close camera apps and the old manually launched Bridge window
   (Ctrl+C). Keep the physical C270 connected.
3. Double-click AvCamSetup.cmd and accept the Windows UAC prompt.
4. If an existing matching AvCam Device MFT is registered, the setup
   leaves that *working DLL* alone. It only installs the persistent
   OBS Bridge controller and registers the logon task.
5. If AvCam is not registered, setup runs the original opt-in C270
   installation. Confirm only for your own Logitech C270.
6. Start OBS, configure a scene, and click Start Virtual Camera.
   The AvCam Bridge waits and starts streaming without another
   administrator console.
7. Select Logi C270 HD WebCam in Windows Camera. Supported
   1280x720 MJPEG paths show the OBS scene.

START MENU CONTROLS
AvCam Physical C270 -> AvCam - Start:
  Starts the persistent background watcher (requests UAC if needed).
AvCam Physical C270 -> AvCam - Stop:
  Stops the bridge. Its sample timestamps then expire and supported
  camera clients fall back to original Logitech video.
AvCam Physical C270 -> AvCam - Status:
  Lists device registration, task status and publisher processes.
AvCam Physical C270 -> AvCam - Remove Autostart:
  Removes only the scheduled task; Device MFT remains registered.
AvCam Physical C270 -> AvCam - Uninstall:
  Stops/unregisters the task and invokes the documented Device MFT
  rollback. Reopen camera apps; reboot if a camera process held the DLL.

There are also AvCamStatus.cmd and AvCamUninstall.cmd in the archive.

DEEP LIVE CAM 2.2.4 SOURCE AUDIT (2026-10-21)
Official tagged source: https://github.com/CRSD-Lau/deep-live-cam/tree/v2.2.4
modules/video_capture.py: Windows uses pygrabber DirectShow enumeration,
then cv2.VideoCapture(index, cv2.CAP_DSHOW, params) first. Fallbacks are
cv2.CAP_MSMF and cv2.CAP_ANY. The index was obtained by DirectShow and is
not guaranteed to identify the same device on MSMF.
modules/ui.py: _start_live_capture constructs VideoCapturer and calls start.
The reported "the selected camera could not be opened" is raised when
VideoCapturer.start returns false. This is an open-path error, not evidence
of an OBS Bridge failure, bad MJPEG contents, or a feedback loop.
Default VideoCapturer.start request is 960x540 at 60 fps; actual UI settings
may override dimensions and fps. The AvCam Device MFT only replaces MJPG
1280x720 samples, so media negotiation and backends must be logged.
OWNER WINDOWS ACCEPTANCE FINDINGS: Device MFT + OBS published frames work in
Windows Camera and Chrome when Deep Live Cam is closed. With Deep Live Cam
already running (staged Bridge start), Chrome reported the physical C270
was used/blocked. With Bridge not publishing, Deep Live Cam could start
while Device MFT was registered. These observations are compatible with
exclusive source ownership or an incompatible simultaneous capture path,
but DO NOT prove which layer holds a lock.
Do not assume a Device MFT can route different physical-vs-processed
frames per application; the existing design cannot guarantee this.
Do not change the installed original Deep Live Cam. Do not enable the
experimental Device MFT automatically. Keep rollback available.

ADDITIONAL DEEP LIVE CAM v2.2.4 OUTPUT CONFLICT (SOURCE VERIFIED)
The tagged Deep Live Cam modules/ui.py _on_live explicitly sets
modules.globals.virtual_cam = True, and _start_live_capture starts a
VirtualCameraSink before it opens the physical input via VideoCapturer.
runtime/virtual_cam.py opens pyvirtualcam.Camera in a background thread.
modules/core.py exposes --virtual-cam-name; by default name is None
and pyvirtualcam chooses its own supported output backend/device.
Therefore Deep Live Cam may attempt to PUBLISH to OBS Virtual Camera
at the same time OBS Studio Start Virtual Camera is publishing there.
This is distinct from Deep Live Cam's physical DirectShow input and
distinct from AvCam Physical Bridge's read-only OBS shared-memory input.
Do NOT assume camera sharing or modify Windows Frame Server registry
until the actual pyvirtualcam selected device/backend has been checked.
The live preview can potentially remain usable after the virtual sink
fails, since the sink runs in a separate thread, but that is not a
guarantee of the whole physical C270 -> Chrome workflow.
For the existing unmodified application, prefer OBS Window Capture
of its preview and ensure Deep Live does not own OBS Virtual Camera.
No code path in AvCam Physical Bridge opens the real C270.

DEEP LIVE CAM + OBS (avoid competing camera publishers)
When Deep Live Cam is processing the real Logitech C270, keep these
roles distinct:
    Logitech C270 -> Deep Live Cam (capture)
    Deep Live Cam Live Preview window -> OBS Window Capture
    OBS Start Virtual Camera -> AvCamPhysicalBridge -> Camera Device MFT
Do not enable Deep Live Cam's own output to "OBS Virtual Camera" while
OBS Studio is also running Start Virtual Camera. The OBS virtual
camera output is a single publisher; two programs cannot reliably
publish to it concurrently. If Deep Live Cam uses pyvirtualcam, select
a separate compatible device for its output or use window capture.

OBS must NOT also try to capture Logitech C270 as another Video Capture
Device source if Deep Live Cam holds that webcam exclusively. AvCam
Bridge never opens the physical camera directly and can remain running
while Deep Live Cam is open. It cannot override exclusive device access
held by another app or start OBS Virtual Camera when another producer
owns the output. Supported Camera Frame Server clients may share the
physical camera, but DirectShow/OpenCV capture behavior can differ.

CAUTION ABOUT VIDEO LOOPS
The C270 Device MFT modifies suitable Media Foundation camera samples.
If Deep Live Cam itself acquires the post-MFT Logitech stream, it may
receive the OBS output as its input and cause a recursive feedback loop.
This combination is NOT tested; use a camera input that does not depend
on the modified output pipeline, or avoid feeding OBS back into the
same capture chain. Do not try to defeat device permissions or sharing.

TROUBLESHOOTING OBS OUTPUT
"Failed to start virtual camera" in OBS is an OBS producer problem,
not proof that Logitech USB is unavailable. Check whether Deep Live
Cam is publishing to the same "OBS Virtual Camera" device. When OBS
Virtual Camera fails to publish, AvCam logs a bounded "mapping exists
but no fresh video frames" warning and waits without touching Logitech.
Stop the competing OBS camera publisher, then start OBS Virtual Camera.
AvCam automatically reconnects; no reboot or driver reinstall needed.
The AvCam device transform leaves the original Logitech capture intact
when fresh OBS frames are not available. The exact host app must still
allow physical camera access.

LONG-RUN BEHAVIOR
At logon the bridge starts with highest privileges for the current
interactive user. The process waits for OBS Virtual Camera, publishes
fresh 1280x720 MJPEG, and reconnects automatically if OBS restarts.
Only one bridge can publish concurrently. If Camera Frame Server
retains the old mapping across a restart, a new bridge may reuse it
without overwriting a running publisher. Missing/stale OBS frames
cause the Device MFT to pass through live sensor frames.

ROLLBACK / SUPPORT
If Windows Camera stops working, close camera apps and run
AvCamUninstall.cmd as administrator. If setup never completed,
an administrator can run:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamPhysicalInstall.ps1 -Disable
Do not manually delete C:\Program Files\AvCamPhysicalC270 while
Device MFT remains registered. Do not replace its DLL while
Windows Camera, Frame Server, or a browser is using it.
The setup does not touch the existing UdeCx AvCam test installation.

COMPATIBILITY AND SECURITY
Windows 11 x64, Logitech C270 USB VID_046D&PID_0825.
Uses Microsoft's sample Device MFT adapted for OBS.
Only MJPEG 1280x720 media types with matching input and output
are eligible for replacement. Other capture paths can continue
showing the real sensor. This software does not modify or emulate
a USB identity, and cannot guarantee acceptance by security or
identity-verification systems. Avoid using it to misrepresent device
identity in verification workflows.

BUILD / VALIDATION
CI builds both native binaries on Windows and validates PowerShell
scripts, plus runs a producer selftest for publisher take-over with
a retained shared-memory handle. This is not a hardware acceptance
test, and the RC must not be marketed as universally compatible.
