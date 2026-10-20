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
