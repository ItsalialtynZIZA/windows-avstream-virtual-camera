AvCam C270-compatible USB/UVC 720p TEST PROFILE
================================================
University research profile for comparing a physical Logitech C270 with
the AvCam UdeCx-emulated USB Video Class (UVC) camera.

WHAT IT DOES
- Preserves the separate AvCam research identity VID_1234&PID_5679.
- Advertises a 1280x720, 30 fps MJPEG VideoStreaming frame instead of
  the existing AvCam research profile's 1920x1080 MJPEG mode.
- Encodes actual 1280x720 MJPEG frames in AvCamPreviewC270.exe.
- Exposes product string "AvCam C270-Compatible USB Camera".
- Uses Windows usbvideo.sys and the existing native UdeCx USB stack.
- Uses native OBS Virtual Camera shared memory as the source.
- AvCamCompareUsb.ps1 produces a read-only JSON PnP comparison with a
  physical Logitech C270 if both are present.

LIMITATIONS
This is a compatibility experiment, NOT a genuine Logitech C270.
It does not use Logitech USB VID/PID, manufacturer/serial information,
firmware or proprietary extensions. Physical host topology and device
identity can distinguish it from actual hardware. Do not represent it
as an original Logitech device or use it to bypass camera verification.

IMPORTANT: PACKAGE AND DRIVER VERSION MUST MATCH
AvCamPreviewC270.exe emits 1280x720 JPEG. An installed old AvCam UdeCx
1920x1080 controller will reject these frames. Changing EXE alone DOES NOT
change the installed USB/UVC descriptors. Full 720p testing requires
manually installing the matching test-signed UdeCx driver under your
explicit approval and following the existing backup/rollback procedure.

The old AvCamPreview.exe and original UdeCx driver may be kept together
for a fully working 1920x1080 fallback. Do not mix 720p and 1080p modes.

READ-ONLY REPORT
From PowerShell:
  .\AvCamCompareUsb.ps1
This saves AvCam-C270-Usb-Comparison.json under the user's Desktop.
No administrator access, registry changes, device uninstall, or network
access are required for the report.

SAFETY
Kernel driver installation can crash Windows. A CI-built test certificate
is not trusted on your PC by default; follow the manual local signing
procedure approved for your own experimental Windows machine. Do not
disable security features silently, uninstall Logitech, or replace the
physical camera's driver. Keep a recovery and rollback route available.
