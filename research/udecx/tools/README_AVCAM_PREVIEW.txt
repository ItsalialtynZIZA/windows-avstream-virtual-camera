AVCAM PREVIEW — Windows 11 x64
============================

AvCam Preview is the FIRST FUNCTIONAL DESKTOP BUILD of the physical
Logitech C270 / OBS capture interface.

Run:
  AvCamPreview.exe

Choose:
  "Logi C270 HD WebCam (USB)" for the exact physical Logitech USB source
  "OBS Virtual Camera" for the virtual OBS capture source

Click "Начать" (Start). The live 1280x720 camera frame appears in the
app window; the status line reports received frames and observed FPS.
Click "Остановить" (Stop) to release the webcam.
Closing the window shuts down the capture worker.

If the camera is busy:
- Close Windows Camera, OBS, and browser tabs currently capturing C270,
  then click Start again.
- For OBS source, launch OBS and Start Virtual Camera first.
- Windows privacy camera permissions must allow desktop applications.

AVCAM BRIDGE (advanced command-line executable):
  AvCamUdeObsFeeder.exe --c270 --continuous --dry-run
  AvCamUdeObsFeeder.exe --obs --continuous --dry-run
  Ctrl+C to stop.

AvCam Preview does NOT access or install any kernel driver. It does NOT
pretend to be a physical USB camera, create a new camera device, or
feed frames to Chrome. It is a local live-image preview milestone only.
AvCamUdeObsFeeder.exe includes upload support for a future operational
UdeCx controller, but the current stock UdeCx build has USB autoplug
and native video ISO disabled. Browser camera availability is therefore
not yet provided by this package.

Neither executable writes images to disk, installs drivers, changes boot
policy, or requires Administrator access. Only use CI downloads built
from your own repository and check SHA256SUMS.txt.
