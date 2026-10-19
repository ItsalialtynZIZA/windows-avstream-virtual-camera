AvCam Physical Logitech C270 / OBS experiment
===============================================
OBJECTIVE
Use the real Logitech C270 USB device (USB\VID_046D&PID_0825 and its
Microsoft usbvideo.sys driver) with a user-mode Camera Device MFT.
For supported Media Foundation clients requesting 1280x720 MJPG only,
replace video samples using the latest OBS Virtual Camera JPEG frame.
All other video formats and missing/stale OBS frames pass through.

IMPORTANT LIMITATIONS
This is a NEW, unverified-on-owner-PC research experiment based on the
Microsoft Windows-driver-samples/avstream/sampledevicemft reference.
It is NOT firmware modification. It does NOT write video back into the
Logitech hardware or guarantee operation in every browser, DirectShow
client, or Windows Camera configuration. It is not invisible to OS
diagnostics. Use for authorized camera processing, not identity evasion.
A COM Device MFT DLL and a separate foreground OBS publishing process
remain necessary. The C270 hardware VID/PID remain unchanged.

WHAT IS INCLUDED
AvCamPhysicalBridge.exe    Reads native OBS NV12 shared memory, encodes
                           MJPEG 1280x720 and publishes to a restricted
                           Global\AvCamPhysicalMjpegV1 shared memory.
AvCamPhysicalDeviceMft.dll Adapted Device MFT compiled from Microsoft's
                           pinned sample; narrowly replaces 1280x720 MJPG
                           samples if fresh data is available.
AvCamPhysicalInstall.ps1   Read-only by default; explicit registration and
                           reversible removal (no USB driver installation).

RECOMMENDED FIRST CHECK
1. Preserve a restore point or full backup and close camera apps.
2. Extract the artifact to a folder.
3. From Administrator PowerShell:
      cd "<extracted folder>"
      powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamPhysicalInstall.ps1
   Inspect physical USB camera and interface paths. No OS changes yet.
4. If the preflight identifies exactly one physical Logitech interface,
   and you explicitly accept experimenting with a camera extension:
      powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamPhysicalInstall.ps1 -Enable
   When prompted, type ENABLE_PHYSICAL_C270.
5. In OBS Studio show the original Logitech C270 and click
   Start Virtual Camera. Run AvCamPhysicalBridge.exe as Administrator.
   Console should print FramesPublished=90 etc.
6. Open Windows Camera and choose the physical Logitech C270 device.
   Check OBS output on compatible 1280x720 MJPG capture paths.
   Some apps may use YUY2/NV12 or non-Frame-Server paths and will
   continue seeing the original real sensor frames. That is expected.
7. If any error occurs, close camera apps and immediately revert:
      powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\AvCamPhysicalInstall.ps1 -Disable
   The real Logitech hardware/usbvideo.sys USB driver was not replaced.

SECURITY AND OPERATION
The publisher creates a restricted Global\ file mapping that grants
read-only access to Windows camera service accounts, write access only
to Administrators and SYSTEM. Run only a copy you trust. The publisher
automatically stops on OBS stream failure and does not persist frames.
If publishing stops, the MFT reverts to the real camera samples instead
of replaying outdated frames. The original working UdeCx AvCam
experiment remains installed separately unless you change it yourself.

PROVENANCE
Upstream reference Microsoft/Windows-driver-samples, commit
2dc3fd3a0cc84a2933f2194e7ec0871584979071
avstream/sampledevicemft (see Microsoft's upstream license).
The AvCam-specific changes are in research/physical-c270.
