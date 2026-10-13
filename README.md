# Windows AVStream Virtual Camera

Experimental Windows 11 x64 virtual camera project.

## Target pipeline

```text
Physical USB Camera
        |
        v
       OBS
        |
        v
 User-mode feeder
        |
        v
 AVStream virtual camera driver
        |
        v
 Windows Camera subsystem
        |
        v
 Browser / desktop application
```

## Development target

- Windows 11 x64
- Visual Studio 2022
- Windows 11 SDK
- Windows Driver Kit (WDK)
- C++17 for the user-mode feeder

## Current milestone

1. Build and load a test-signed AVStream camera driver.
2. Expose one camera device to Windows.
3. Advertise one uncompressed 1280x720 @ 30 fps mode.
4. Accept frames from a user-mode feeder.
5. Verify the camera in Windows Camera and browser enumeration.

> This project creates a Windows camera capture device. It does not emulate a physical USB bus device and is not guaranteed to appear in VirtualBox USB passthrough.
