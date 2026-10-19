AvCam Bridge — user-mode video source and MJPEG uploader
========================================================

This is the first continuously running APP component of the AvCam
UdeCx project. It reads an actual Windows Media Foundation camera
(Logitech C270 or OBS Virtual Camera), converts 1280x720 BGRA frames
into 1920x1080 MJPEG, and sends them to AvCamUdeController0 using
the bounded IOCTL_AVCAM_UDE_SUBMIT_MJPEG upload protocol.

This EXE contains NO installer, kernel driver or USB/PnP mutations.

QUICK START — C270 (runs without AvCam driver)
PowerShell, normal Windows user:
    .\AvCamUdeObsFeeder.exe --c270 --continuous --dry-run

QUICK START — OBS (runs without AvCam driver)
In OBS, add the real C270 as capture source and start OBS Virtual Camera.
Configure OBS virtual camera to output 1280x720 at 30 FPS (the MF reader
requires a 1280x720 BGRA-compatible input). Then:
    .\AvCamUdeObsFeeder.exe --obs --continuous --dry-run

Press CTRL+C once to stop either mode. --dry-run captures and encodes,
but does not touch the controller or create a USB camera.

LIVE UPLOAD — requires a separately installed and enumerated UdeCx
research controller (NOT shipped in this artifact):
    .\AvCamUdeObsFeeder.exe --c270 --continuous
    .\AvCamUdeObsFeeder.exe --obs --continuous

The live mode first opens \\.\AvCamUdeController0. If the controller
is not installed or the current user lacks access, it fails immediately
without opening the webcam. This does not automatically install, create,
or enumerate a UdeCx USB camera. The current UdeCx enumeration-only
driver DOES NOT ENABLE native ISO streaming, so success in upload cannot
be claimed as a frame received by Windows USBVIDEO or a browser.

The controller's research SDDL permits SYSTEM/Administrators only.
A future host integration should provide controlled app permissions or
a broker service, rather than routinely running capture elevated.

Other arguments:
    --help
    --device-name "Logi C270 HD WebCam" --source-usb-id VID_046D&PID_0825
    --frames 120
    --continuous
    --dry-run

--frames and --continuous are mutually exclusive.
When writing to a live controller, a busy frame slot is dropped rather
than retrying an old frame and adding latency. Continuous mode prints
cumulative counters roughly every five seconds. OBS and C270 should
not be opened simultaneously by competing capture applications.

This research driver advertises only its own experimental VID:PID
1234:5679, never the physical Logitech VID:PID 046D:0825.
Do not present an emulated camera as genuine Logitech hardware.
