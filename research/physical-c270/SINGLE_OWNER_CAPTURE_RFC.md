# AvCam: single-owner physical C270 feasibility gate

Status: DESIGN AND FEASIBILITY ONLY. No solution tested on the owner's PC.

## Fixed requirements
- One real Logitech C270 (USB VID_046D PID_0825) remains installed.
- Unmodified, installed Deep Live Cam Studio v2.2.4 continues to process live
  camera input.
- Chrome enumerates and selects the EXISTING physical Logi C270 HD WebCam
  endpoint, not a newly created software camera.
- Chrome receives processed OBS frames without a recursive input loop.
- Changes must have explicit opt-in and rollback; current Device MFT is off.

## Actual hardware evidence
1. Chrome sees OBS test frames through existing C270 Device MFT when Deep Live
   Cam is not capturing.
2. Without any AvCam Device MFT or Bridge, Deep Live Cam can capture C270 and
   Chrome fails to open it with 'used or blocked by another application'.
3. Owner tried UVC EnableDshowRedirection bit 1, disconnected/reconnected C270,
   and still saw same simultaneous-open failure. The -Restore operation
   reported RESTORED_ORIGINAL_DSHOW_VALUE.
4. Windows FrameServer running alone did not resolve contention.
5. Original Deep Live Cam source v2.2.4 tries OpenCV CAP_DSHOW first, then
   CAP_MSMF and CAP_ANY; its runtime active backend has NOT been verified.

## Important limits from Microsoft documentation
- DShow Bridge may allow DirectShow and shared clients, but enabling its
  device opt-in flag is not sufficient on this tested system.
  https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dshow-bridge-implementation-guidance-for-usb-video-class-devices
- A Frame Server Custom Media Source can mark streams as shared, but it is
  attached via its own stub-driver registered camera interface. This DOES NOT
  transparently replace the existing Logitech USB device's already-registered
  camera interface.
  https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source
- The Microsoft Windows-Camera VirtualCamera sample CameraWrapper can wrap
  existing hardware, but still uses a software camera endpoint. Not a match
  for strict physical endpoint identity.
  https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera

## Feasibility gate: before implementing a broker
A single-owner broker only solves hardware contention if both consumers
consume ITS output, rather than independently opening the same physical
USB pin. The original Deep Live Cam is unmodified and its camera selector
opens an enumerated capture device via DirectShow by index. If it remains
configured to open physical C270, a separate broker cannot own C270 first
without reproducing the conflict. If it is configured to read a separate
software camera, Deep Live Cam itself remains unmodified, but this changes
its selected INPUT device; and a second implementation must still enable
Chrome to open the physical C270 endpoint without triggering hardware
contention.

A Device MFT changes samples only once the camera capture path has opened.
It does not override the physical camera driver's open/stream ownership.
No existing code establishes per-client raw-vs-OBS frames on the same
physical endpoint. Therefore a proof of feasibility must verify:
  A. Which capture backend the installed release actually uses (instrument
     only an independent test executable or inspect logs; no modification
     of the installed Deep Live Cam).
  B. Whether an OS-supported extension point can supply frames to a
     Chrome capture of the *same existing camera interface* without
     separately opening the physical UVC pin.
  C. If B exists, that it can maintain capture continuity and preserve
     the USB camera's original identity without masking descriptors.
  D. If B is unsupported, report the strict requirement as blocked
     rather than silently substituting a virtual-camera endpoint.

## Rejected shortcuts
- Change EnableFrameServerMode or EnableDshowRedirection repeatedly.
- Present the virtual camera with a Logitech-like friendly name and claim
  the actual physical USB C270 is selected.
- Modify the installed Deep Live Cam, firmware or physical USB descriptors.
- Build a broker that creates a second consumer of the physical KS capture pin
  and assume it bypasses the concurrency limit.
- Register a Device MFT and claim it alone makes a busy physical source
  independently openable in Chrome.

## Deliverables for engineering
1. Read-only installed-build capture-backend evidence (process- and camera-
   scoped) and a result file with no sensitive user data.
2. Minimal prototype of a single-owner frame relay in a separate test
   process with mock input; prove fanout and timestamp continuity WITHOUT
   touching the physical C270 or any system registry keys.
3. An explicit adapter feasibility decision for the existing C270 Chrome
   device interface. If no supported adapter is identified, stop before
   driver deployment and present constraints to the owner.
4. Only after the feasibility gate: integration, opt-in installation,
   rollback and simultaneous Deep Live Cam + OBS + Chrome acceptance test.
