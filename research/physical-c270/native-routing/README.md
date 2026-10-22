# AvCam native per-consumer routing — strictly gated development

## Objective

Keep **one original physical Logitech C270 endpoint** in unchanged Windows
applications. A Deep Live Cam session must get original RAW bytes while an
unchanged Chrome session must get processed OBS bytes through that exact
same physical endpoint. No browser extension, second virtual device or
friendly-name-only matching qualifies as a working product.

## Delivered in this increment

`AvCamPerClientIsolation.h` is a native C++17 **policy and frame-copy
simulation**, not a Windows camera driver or capture integration.

- Two independently assigned sessions, RAW and processed OBS.
- Exact original physical camera interface/PnP matching is required; a
  software-virtual-camera interface or matching friendly name is rejected.
- Strictly refuses sessions missing a *trusted per-client context proof*.
  The actual Windows adapter that would produce such a proof **does not
  exist in this repository**. Simulation tests supply artificial proof
  objects ONLY to test routing rules.
- OBS loss invalidates only OBS. An OBS client receives **no frame**, never
  raw Logitech fallback. RAW sessions keep their original pixels.
- Bounded freshness checks, per-client independent frame copies, per-source
  epoch/sequence protection, explicit detach, concurrent publisher/reader
  serialization, and shutdown cleanup.

Run the no-driver simulation using any C++17 compiler:

    g++ -std=c++17 -O2 -pthread -Wall -Wextra -Werror -pedantic \
      research/physical-c270/native-routing/AvCamPerClientIsolationTest.cpp \
      -o /tmp/avcam-isolation
    /tmp/avcam-isolation

GitHub Actions: .github/workflows/avcam-native-readonly.yml, job
`strict-two-client-simulation`. No Windows camera is opened.

## UNRESOLVED Windows native attachment

The simulation intentionally has **zero bridge into unchanged Chrome,
Deep Live Cam or FrameServer**. Windows documented DMFT processing is
per-device / stream and does not furnish an established authenticated
per-consumer callback that can inject distinct pixel bytes through one
unchanged physical C270 endpoint. A fake checkbox or process-name test
is not sufficient native client attestation.

The tested software camera made a **separate** endpoint and was
associated with a real Win11 Camera Frame Server hang. It remains
quarantined. UdeCx creates an emulated endpoint, not the original USB
instance. The previously demonstrated Chrome extension performs
JS-level routing and is explicitly not the target.

## Integration gate

Do not call the simulation a driver/endpoint or ship as a camera.

1. Document the exact Microsoft-supported camera API/driver callback
   capable of assigning an output sample to an *individual* authenticated
   unchanged client after shared physical capture.
2. Demonstrate that the physical C270 PnP interface path is unchanged
   and that a deep-live consumer and browser consumer are individually
   distinguished without heuristics.
3. Add an adapter implementing that proven per-client context with no
   raw feedback, no OBS->RAW fallback and no global MFT mutation.
4. Isolated validation: no FrameServer hang, 30fps processed, 30fps RAW,
   repeated detach/restart, crash recovery and reversible shutdown.
5. Only after all safety gates, test on the primary user laptop.

Without step 1, the strict feature is blocked; don't invent a Windows
API, claim equivalence of virtual/logical and physical devices, or
encourage unreviewed low-level hooks.

## Microsoft documentation

- Device MFT architecture: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dmft-design
- DShow bridge/client sharing: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dshow-bridge-implementation-guidance-for-usb-video-class-devices
- Virtual camera's arbitration/exclusive physical source:
  https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-imfvirtualcamera-adddevicesourceinfo
