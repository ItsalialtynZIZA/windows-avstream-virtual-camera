AvCam C270 / OBS MJPEG capture smoke test
=======================================

User-mode Windows 11 x64 only. This executable NEVER installs, starts,
updates, or accesses a USB/AvCam kernel driver. It does not create an
emulated camera and does not send frames to websites or save frames.

Tests ONLY: Windows Media Foundation capture of 1280x720 BGRA frames,
then Windows Imaging Component encoding of 1920x1080 MJPEG,
followed by full in-memory JPEG decoding to a 1920x1080 BGR raster
for EACH frame (not just testing JPEG markers).

PREPARATION:
- Close OBS, browser tabs and camera applications if testing C270 directly.
- Connect the Logi C270 HD WebCam (USB VID_046D&PID_0825).
- Extract the entire artifact ZIP into a normal folder.
- Open PowerShell in that folder as a NORMAL user (not Administrator).

Commands:
  .\AvCamUdeCaptureSmoke.exe --list
  .\AvCamUdeCaptureSmoke.exe --self-test
  .\AvCamUdeCaptureSmoke.exe --frames 30

Expected end-of-run marker on success:
  AVCAM_C270_MF_MJPEG_DECODE_PASSED
  AVCAM_C270_MF_MJPEG_SMOKE_PASSED

Alternate OBS pathway:
- Add C270 to OBS at 1280x720/30fps.
- Start OBS Virtual Camera. Make OBS output 1280x720/30fps for the
  current MF capture reader, which expects 1280x720 RGB32 input.
- Close other apps using the OBS Virtual Camera.
- Run:
  .\AvCamUdeCaptureSmoke.exe --obs --frames 30

Exit codes:
  0  success
  2  invalid command arguments
  3  Windows COM initialization failed
  4  Windows WIC JPEG encoder unavailable
  5  camera identity configuration invalid
  6  MF camera missing, busy, ambiguous, or RGB32 output unavailable
  7  MF camera frame read failed
  8  JPEG encode/validation failed
  9  JPEG SOI/EOI marker invalid
  10 insufficient captured frames
  11 full JPEG decoded pixel data invalid or unsupported dimensions

The --self-test command works WITHOUT a webcam: it encodes a deterministic
1280x720 test image, fully decodes the produced 1920x1080 JPEG in RAM,
and prints AVCAM_MJPEG_OFFLINE_SELF_TEST_PASSED on success.
The host test prints FramesCaptured, FramesEncoded and FramesDecoded.
A zero-frame or failed run is not evidence of any USB Video Class success.
Even a successful smoke run does NOT prove the UdeCx driver, its ISO URBs,
Windows usbvideo.sys, or Media Foundation camera output from AvCam.
The driver safety flags remain off.

Check the executable's SHA256 hash using:
  Get-FileHash .\AvCamUdeCaptureSmoke.exe -Algorithm SHA256

The expected hash is published in the GitHub Actions run log AND in
the downloadable SHA256SUMS.txt. The executable is not code-signed.
Only use the artifact from your own repository CI.

PERFORMANCE DIAGNOSTICS:
- TimingCaptureMs: cumulative Media Foundation frame-read time.
- TimingEncodeMs: cumulative BGRA->1920x1080 JPEG conversion time.
- TimingDecodeMs: cumulative full 1920x1080 JPEG pixel decode time.
- ObservedProcessingFPS: end-to-end smoke-test throughput, not necessarily
  physical webcam sensor FPS. This includes test-only full decode overhead.
- MfSampleTimestampSpan100ns: first-to-last sample timestamps from MF.
- NonIncreasingTimestamps: repeated/decreasing MF sample timestamps.
- AVCAM_SPEED_BELOW_25FPS: advisory only; successful correct frames still
  result in exit code 0. Compare capture / encode / decode before tuning.
