AvCam UdeCx HOST VIDEO EXPERIMENT — Windows 11 x64
=======================================================

This is an EXPERIMENTAL kernel-mode USB host controller and user-mode
AvCam Preview integration for a PHYSICAL Windows 11 x64 computer.
It may cause a BSOD or an unbootable Windows installation. It has not
yet passed physical-device UVC/USBVIDEO/Media Foundation acceptance.
No kernel driver is ever installed by GitHub Actions.

TEST IDENTITY (NOT the actual Logitech identity):
  Controller: ROOT\AvCamUdeController
  USB child:  USB\VID_1234&PID_5679
  Logitech C270 input: VID_046D&PID_0825 (source only)
  The installed legacy ROOT\AvCamVirtualUsbHost is NOT modified.

THESE FILES ARE SEPARATE FROM THE NORMAL SAFETY-GATED DRIVER.
Only the separately acknowledged host-video build enables USB autoplug
and the native high-speed MJPEG ISO endpoint. The default Release build
remains USB-unplugged and ISO-disabled. A successful upload IOCTL is not
proof that a USBVIDEO/Windows Camera sample was received.

RECOVERY BEFORE ANY ATTEMPT:
1. Ensure you have a full backup and separately saved BitLocker recovery key.
   Prepare Windows Recovery Environment / bootable Windows recovery USB.
2. From elevated PowerShell: run
     .\AvCamUdeHostPreflight.ps1 -CreateRestorePoint
   If Checkpoint-Computer is unavailable, arrange a verified full-system
   image backup. Do not assume System Restore is sufficient to recover
   from a faulty boot-start or early-loading driver.
3. Check Secure Boot and test-signing requirements. A GitHub Actions test
   certificate is NOT trusted by your PC by default. Do not import an
   unknown certificate and do not change boot settings without considering
   BitLocker/Device Encryption and Secure Boot implications.
4. Use only the verified build and SHA256SUMS.txt; the test-signing
   certificate is for isolated experiments, NOT a production certificate.

OPTIONAL MANUAL INSTALL (ADMINISTRATOR; NO AUTO-INSTALL IN ARCHIVE):
  .\AvCamUdeHostRootInstall.exe install .\AvCamUde.inf I_ACCEPT_KERNEL_TEST_ON_PRIMARY_WINDOWS
The tool requires physical Windows 11 x64 and refuses VM-like SMBIOS
hardware and non-elevated execution. It creates/updates ONLY the new
ROOT\AvCamUdeController identity.

VIDEO SOURCE:
Use AvCamPreview.exe from the separate 'AvCam-Bridge-Windows-x64'
artifact, select the physical C270 or OBS virtual camera, opt in
to 'Передавать MJPEG в UdeCx', and press Start. Controller upload
currently requires Administrator privileges by design (SY/BA SDDL).
Do not assume a connected browser camera just because the preview
shows moving images or the UdeCx upload counter increments.

READ-ONLY HOST ACCEPTANCE (before/after any authorized installation):
  .\AvCamUdeHostAcceptance.ps1 -OutputPath .\AvCamHostReport.json
This inventories ONLY the new research UdeCx controller and experimental
VID_1234 PID_5679 USB child, including PnP errors and usbvideo.sys service
binding. It never installs or changes drivers or captures Logitech frames.
USBVIDEO_BINDING_DETECTED still does NOT prove a received MJPEG frame.

ROLLBACK (elevated PowerShell, while Windows boots normally):
  .\AvCamUdeHostRollback.ps1
  .\AvCamUdeHostRollback.ps1 -Execute
The first command is read-only; the second removes ONLY the specific
ROOT\AvCamUdeController device. The script does NOT remove or alter
the legacy AvCamBus driver, boot settings, or global driver store.
For a driver-store cleanup, identify the correct published oemNN.inf
in pnputil /enum-drivers and remove that exact INF only after verifying it.
Use offline recovery or your full-system image if Windows fails to boot.

ACCEPTANCE NOT YET MET:
- USBVIDEO must bind to USB\VID_1234&PID_5679.
- A consumer must actually receive valid MJPEG 1920x1080 frames.
- Repeated open/close, unplug/replug, cancellation, RESET and sleep/wake
  must work without data corruption, hangs, or crashes.
