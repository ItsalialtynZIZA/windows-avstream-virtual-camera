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

LOCAL SIGNING AND GUARDED INSTALL (EXPERIMENTAL)
The ZIP's GitHub Actions certificate is NOT a trusted signature on your PC.
The matching UdeCx 720p SYS and catalog must be signed locally with an
existing and trusted test certificate with private key. The package includes
AvCamC270Install.ps1. Run elevated PowerShell from the unpacked directory.

Read-only preflight (checks certificate, signtool and inf2cat):
  .\AvCamC270Install.ps1

ONLY after full backup/recovery preparation and explicit acceptance:
  .\AvCamC270Install.ps1 -Install

The install mode requires exactly one healthy installed UdeCx controller,
exports its current driver package to Documents as backup, makes a separate
copy of the candidate, signs SYS with the local certificate, regenerates
its catalog with Inf2Cat and signs the catalog, verifies both signatures,
then invokes the guarded AvCamUdeHostRootInstall.exe on the current controller.

If a prerequisite is missing, the script stops without reinstalling the
driver. If the updated controller is not accepted by Windows, restore the
exported original driver using the existing documented rollback process.
The script does NOT disable Secure Boot, change test signing, delete device
instances, touch the physical Logitech driver or silently reboot.

After the C270 profile driver is installed/rebooted as needed:
- Keep OBS running and start OBS Virtual Camera with the physical C270.
- Launch AvCamPreviewC270.exe as Administrator.
- Select OBS Virtual Camera; enable MJPEG upload to UdeCx; press Start.
- In Windows Camera select AvCam C270-Compatible USB Camera (or AvCam USB
  name cached by Windows) and verify live video and 1280x720 mode.
- Keep previous AvCamPreview.exe + original driver backup for rollback.

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
