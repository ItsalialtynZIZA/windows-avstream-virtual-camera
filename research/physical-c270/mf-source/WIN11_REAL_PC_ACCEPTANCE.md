# AvCam Windows 11: real-PC acceptance

This checklist is NOT evidence that CI tested actual Windows Camera, Chrome,
Deep Live Cam or a physical Logitech C270. No VM is required.

Scope: an independent virtual camera with processed OBS video. It does not
replace the physical Logitech endpoint on a per-application basis.

## A. Safe read-only inspection (no admin, no installation)

Extract the COMPLETE AvCam-OBS-Live-Diagnostic GitHub artifact. Open
ordinary Windows PowerShell inside the extracted folder:

~~~powershell
Get-ComputerInfo | Select-Object WindowsProductName,OsVersion,OsBuildNumber
.\AvCamSoftwareCameraControl.exe --preflight
.\AvCamMfLocalServiceProbe.exe --status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Manage-AvCamMfSource.ps1 -Action Status
~~~

Windows build must be at least 22000. COM registration should normally be
absent before opt-in. The installed test camera is never the original C270.

## B. Actual LocalService session-0 OBS pipe acceptance

Keep Deep Live Cam on the original Logitech C270 and configure OBS to use
Window Capture of Deep Live Cam (not the physical C270 again). The router
must run with independent OBS/RAW and IPC publishing already enabled.
In ordinary PowerShell, run the processed-only pipe broker:

~~~powershell
.\AvCamMfObsPipeBridge.exe --continuous
~~~

In a separate *elevated x64 PowerShell*, in the same folder:

~~~powershell
.\AvCamMfLocalServiceProbe.exe --probe --accept-temporary-service
~~~

Only after explicit consent, this probe creates one unique TEMPORARY
Windows service running as NT AUTHORITY\LocalService in session 0, checks
three consecutive fresh NV12 OBS samples, and deletes its own service.
The service diagnostic executable is temporarily placed in protected
Program Files (not a user-writeable folder). This does not modify any
physical camera, driver, existing Windows service or COM class.

Success marker: AVCAM_LOCALSERVICE_SESSION0_OBS_PIPE_PASS

Nonzero result: AVCAM_LOCALSERVICE_PIPE_RESULT=N.
102=requires elevation, 103–110=SCM setup/timeout,
120=wrong account, 121=not session 0, 122=frame invalid,
123=stale frame, 124=insufficient processed frames.

The test has a bounded duration. If it times out during shutdown, it
may intentionally retain the protected temporary executable instead of
deleting a binary still in use. Inspect ONLY services beginning with
AvCamMfPipeProbe_ if cleanup is needed.

Success verifies LocalService IPC read permissions and actual video bytes,
NOT that the REAL Windows Camera Frame Server has loaded the MF DLL.

## C. Explicit separate AvCam virtual-camera trial

This is EXPERIMENTAL, CHANGES SYSTEM COM REGISTRATION and should not be
performed until B has passed. Create a Windows restore point/backup first.
No Logitech drivers are modified or uninstalled.

Elevated x64 PowerShell, in matching extracted artifact:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Manage-AvCamMfSource.ps1 -Action Register -AcceptExperimental
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Manage-AvCamMfSource.ps1 -Action Status
~~~

Registration only adds the dedicated AvCam-owned COM class under the
protected Program Files directory; it does not create a virtual camera.

Keep router+OBS-only pipe broker alive. In ordinary user PowerShell:

~~~powershell
.\AvCamSoftwareCameraControl.exe --preflight
.\AvCamSoftwareCameraControl.exe --session
~~~

The explicit --session uses MFCreateVirtualCamera with CurrentUser access
and Session lifetime. Keep it open; press Enter to end. This creates an
INDEPENDENT software camera named AvCam IPC Video / Windows Virtual Camera
(display may differ), not a per-application Logitech override. Privacy
permissions can block creation; record the exact HRESULT.

## D. Windows Camera, Chrome, and Deep Live Cam checks

Confirm the AvCam device appears in Windows Camera and Chrome; select it
and verify live OBS-processed frames, correct orientation, steady 1280x720,
no green/blank screen. At the same time Deep Live Cam must still receive
RAW from physical Logitech, while OBS Window Capture shows the processed
result. Reject any recursive feedback loop.

Capture: Windows build; device name in Chrome; 30-60 seconds of output;
any freezes, errors, jitter, camera privacy prompts; RAW/OBS independence.

This section cannot be completed on hosted GitHub Actions and must be
observed on the actual Windows PC before first release.

## E. Restore previous state

Close Windows Camera/Chrome, press Enter in the virtual camera session,
stop router and broker. Elevated PowerShell:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Manage-AvCamMfSource.ps1 -Action Unregister -AcceptExperimental
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Manage-AvCamMfSource.ps1 -Action Status
~~~

Verify the dedicated AvCam COM class is absent, temporary test service
has been removed, and original Logitech still works normally. No driver
changes, registry changes to other CLSIDs or physical USB replacements.

Full original-Logitech-identity Chrome substitution remains UNSOLVED.
