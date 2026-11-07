# AvCam format compatibility v2 (isolated experiment)

Branch: feat/avcam-format-compat-v2. The validated dev/manual-vhci baseline
and installed C270 DLL are not modified by these commits.

## Actual scope

The source bridge publishes 1280x720 MJPEG and 1280x720 NV12.
The laboratory Device MFT can now match a negotiated input/output subtype
and output dimensions only when the input/output MF pin formats match,
the stream is topologically connected, and the device is using the MFT path.

Formats handled in the lab hook:
- MJPG: original JPEG bytes for 1280x720, otherwise 1280x720 NV12 to
  aspect-preserving resized BGR24 to WIC JPEG at the negotiated dimensions
- NV12, I420, YUY2, UYVY: resized from 1280x720 NV12
- RGB24 (BGR24 byte layout), RGB32 (BGRA with opaque alpha): resized from NV12
- Even frame dimensions from 2x2 to 1920x1080 are bounded by output size;
  practical frame sizes depend on the original camera's advertised MF types.

Not supported: H264, P010/10-bit, YV12, uncommon packed formats,
custom/padded strides, odd dimensions, >1080p, unsupported device media
formats, site-specific camera backends that do not invoke this Device MFT,
or any assurance of compatibility with biometric/liveness services.

The negotiated device is still the physical Logitech. That does not prove
the video originated from its image sensor; the whole MF camera pipeline is
modified and all its MF consumers may see the substituted pixels.

## Fail-safe and installation

- Run v2 as a separate reversible experimental build, never overwrite a
  loaded DLL or install over the original stable baseline.
- If media types differ, are malformed, use unknown strides, or the bridge
  publishes missing/stale frames, preserve the original camera sample.
- The 512-entry connected-upstream media-type enumeration limit remains.
- Both cameras must be physically independent: Deep Live Cam reads the built-in
  camera and the C270 output is selected by the test WebRTC client.
- Reuse the existing opt-in installation/rollback scripts. Back up the
  known-good v1 package and installer state before any device experiment.

## Acceptance

1. Compile the pure-pixel converter on Linux with -Werror and run its synthetic
   format, dimension, crop, packed-byte and bounds tests.
2. Compile both the default pass-through Device MFT and the explicitly gated
   dual-camera DLL on Windows CI. Validate WIC JPEG implementation and symbols.
3. On the owner's test PC only, verify Chrome sample with OBS red test
   frame at VGA / HD, and inspect manifestless ETW SampleReplaced / SampleSkipped.
4. Repeat with 640x480 MJPG and YUY2 pin formats if they are actually
   advertised. Confirm visual changes follow OBS, not a stale source frame.
5. Test restart/fallback with OBS off, rollback to the stable DLL, and reboot
   if FrameServer still has an old DLL loaded.

A green CI build proves only build/test integrity, not site compatibility.
