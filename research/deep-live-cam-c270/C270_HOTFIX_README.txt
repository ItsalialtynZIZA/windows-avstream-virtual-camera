Deep Live Cam Studio 2.2.4 / Logitech C270 - experimental capture fallback
================================================================================

This is a SEPARATE portable CUDA build of the public Deep-Live-Cam v2.2.4 source
(commit 617c733d42a10a2fcba036385e19d66fabcc2cc1) with a capture-only
patch. The original upstream author did not publish or approve this build.

EXPERIMENTAL: Hardware compatibility is not confirmed on the user's C270.
Do not overwrite your installed DeepLiveCamStudio folder; first extract the
portable zip into a separate directory and run DeepLiveCamStudioCLI.exe.
Keep AvCam Device MFT uninstalled until the physical webcam preview works.

PATCH SUMMARY
- Keeps DirectShow camera indexes; never reuses a DSHOW index in MSMF/ANY.
- Tries requested MJPEG format first, then safe 640x480 MJPEG/YUY2/AUTO
  and 320x240 profiles if the source fails a basic frame-rate test.
- Verifies actual decoded BGR frames and measured fps (not just isOpened).
- Releases the previous camera capture handle before changing formats.
- Rejects streams that open but do not produce enough valid frames.
- Keeps one-frame callback and VideoCapturer API for upstream UI compatibility.

LIMITATIONS
- No physical hardware CI test; passing build/tests does not prove the
  distorted picture is fixed. Corrupt frames may originate elsewhere.
- Capture probe takes a few seconds; access blocked by another program is
  not bypassed. Deep Live Cam and OBS cannot both publish to OBS Virtual
  Camera simultaneously.
- This fix DOES NOT make a camera into a USB Logitech device and does not
  modify camera drivers, Windows registry, or your AvCam installation.
- No private camera image is written to disk by this patch.

VERIFY
In the portable program, first close Windows Camera and OBS, then start live
mode with the physical Logitech C270. Open the command-line executable and
inspect messages such as:
[VideoCapturer] probe MJPG ... measured=1.0fps
[VideoCapturer] probe YUY2 ... measured=20.0fps
[VideoCapturer] selected Logi C270 HD WebCam: YUY2 640x480 @ 20.0fps
The actual selection and frame-rate depend on your device.
If no mode works, exit the preview and keep the original program.

SECURITY
Face-swap content must be used with permission and clear disclosure
where needed. Do not use this software to circumvent camera or identity
verification systems.

Upstream license, notices, model download policy and dependencies are
included from Deep-Live-Cam. The model files are deliberately NOT bundled.
