Deep Live Cam Studio 2.2.4 — C270 capture hotfix (EXPERIMENTAL)
=================================================================
This package contains only the two rebuilt Windows EXE files. All unmodified
CUDA/OpenCV/ONNX Runtime data is loaded from your existing DeepLiveCamStudio
2.2.4 installation.

SAFETY FIRST
1. Confirm that the installed program shows Deep-Live-Cam 2.2.4 and lives at
   %LOCALAPPDATA%\Programs\DeepLiveCamStudio.
2. Close Deep Live Cam Studio and DeepLiveCamStudioCLI.exe.
3. Leave AvCam Device MFT UNINSTALLED. Stop OBS and Windows Camera.
4. Extract this patch ZIP into a separate folder.
5. Run Install-C270Hotfix.cmd. It saves original EXEs to
   %LOCALAPPDATA%\DeepLiveCamStudio\C270HotfixBackups\<timestamp>
   and replaces only DeepLiveCamStudio.exe and DeepLiveCamStudioCLI.exe.
6. Launch DeepLiveCamStudioCLI.exe from the original install directory.
   Select the Logitech C270 and click Start Live.
   Review [VideoCapturer] probe and selected lines.
7. To revert, close Deep Live Cam and run Restore-C270Hotfix.cmd.

No Windows driver, firmware, AvCam Device MFT or registry key is changed.
The existing Deep Live Cam CUDA runtimes must be compatible with binaries
built from the pinned v2.2.4 source revision. If not, restore the originals
with one click and instead use the full independent portable build.

The fix is source-level, tested with mocked frame sources and compiled in
Windows CI. IT HAS NOT BEEN TESTED ON PHYSICAL C270 HARDWARE. Performance
and image integrity are not guaranteed by compiler/unit test results.

The default capture mode is still MJPEG. If actual capture falls far below
requested FPS, the patch tries MJPEG at a reduced resolution, YUY2 and native
DirectShow formats, always releasing the prior capture before changing mode.
It never assumes DirectShow and Media Foundation use the same camera index.

This patch is a community experimental modification, not an official release.
Use with consent; do not use synthetic video to bypass identity checks.
