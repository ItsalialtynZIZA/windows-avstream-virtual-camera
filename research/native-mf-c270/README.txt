Native Windows Media Foundation Logitech C270 Capture — experimental standalone input module

Goal:
    Physical USB Logitech C270 (VID_046D PID_0825 MI_00)
          -> Media Foundation native source reader (no OpenCV video capture)
          -> BGR frames for Deep Live Cam processing
          -> Deep Live Cam LIVE PREVIEW WINDOW (OBS virtual output disabled)
          -> OBS Window Capture -> OBS Virtual Camera.

This build DOES NOT claim that a processed stream becomes a real USB camera.
AvCam Device MFT was previously associated with a black physical camera
preview and must remain uninstalled pending separate driver-side repairs.

Usage:
1. Check the physical Logitech in Windows Camera with Deep Live Cam/OBS closed.
2. Extract bundle, close camera programs, run INSTALL_NATIVE_MF.cmd.
   The installer backs up original Deep Live Cam 2.2.4 EXE files.
3. Run START_PREVIEW.cmd (launches the console version of Deep Live Cam).
4. Select Logi C270 HD WebCam, click Start Live.
5. If the native MF source fails, read the [DLC.MF] messages. It never falls
   back to a different camera or silently publishes a fake black image.
6. After the preview shows normal frames, configure OBS Window Capture and
   start OBS Virtual Camera. OBS must be the sole virtual camera publisher.

To undo: Close Deep Live Cam and run RESTORE_ORIGINAL.cmd.

Windows CI validates build and the C API ABI. CI does not contain Logitech
C270 hardware, so actual image quality/FPS on the user's webcam is unverified.

No USB firmware or driver changes, no kernel drivers, and no registry edits.
Use synthetic images only with appropriate consent and disclosure.
