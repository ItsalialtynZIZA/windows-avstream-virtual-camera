AVCAM UDECX - FIRST USB ENUMERATION LAB (Windows 11 VM ONLY)
==================================================================

Status: EXPERIMENTAL KERNEL DRIVER. USB enumeration is UNVERIFIED.
This build plugs a test VID_1234&PID_5679 USB child on controller D0 entry.
ISO MJPEG remains explicitly DISABLED. There will be NO working image.
It is NOT a replacement for a real Logitech device, and its identifiers
must not be used to impersonate the physical USB camera.

STRICT PREREQUISITES:
1. Use a disposable Windows 11 x64 VM (VMware, VirtualBox, Hyper-V, etc.)
   with >=4 GB RAM and >=8 GB free space.
2. Shut down the VM and take a host-side restore snapshot/checkpoint.
   A VM guest script cannot verify that snapshot exists.
3. Read the build's SHA256SUMS.txt, verify file hashes.
4. Do NOT execute this package on your primary Windows laptop.
5. VM test-signing may require disabling VM Secure Boot from the
   hypervisor's VM SETTINGS, not on the physical host, then inside
   the VM running "bcdedit /set testsigning on" as administrator and
   rebooting the GUEST. These steps reduce VM security.
6. Test-signature trust changes must only occur within disposable VM.

PACKAGE:
- AvCamUde.inf, AvCamUde.sys and AvCamUde.cat: VM-only WDK build
- AvCamUdeVmTestSigner.cer: PUBLIC test certificate for this CI run
- AvCamUdeVmRootInstall.exe: independent guest-only installer
- AvCamUdeVmPreflight.ps1: VM identity/readiness, READ ONLY
- AvCamUdeVmEnumerationReport.ps1: USB/PNP driver diagnostics, READ ONLY
- README_VM_ENUMERATION.txt, SHA256SUMS.txt
The private signing key is NOT distributed.
WDK-generated temporary test certificate is NOT a trusted production CA.

RUN FIRST -- READ ONLY, INSIDE DISPOSABLE VM (PowerShell):
  .\AvCamUdeVmPreflight.ps1

STOP if VM_PREFLIGHT_GUEST_READY is missing, or VM snapshot was not
created on the host.

After the user explicitly approves installing into this VM only:
  # Elevated PowerShell IN THE VM, never on physical host
  Import-Certificate -FilePath .\AvCamUdeVmTestSigner.cer -CertStoreLocation Cert:\LocalMachine\Root
  Import-Certificate -FilePath .\AvCamUdeVmTestSigner.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher

Confirm the GUEST has test-signing enabled, and reboot the GUEST if
that boot configuration was changed. DO NOT change physical host BCD.

  .\AvCamUdeVmRootInstall.exe install .\AvCamUde.inf I_UNDERSTAND_DISPOSABLE_VM_ONLY

This action creates/updates Root\AvCamUdeController using SetupAPI and
starts the TEST-SIGNED KERNEL driver. It is a MUTATING action with a
real risk of VM crash/BSOD. The executable independently verifies VM
SMBIOS fields before installing. It is NOT a secure hypervisor attestation.

Then use the independent READ-ONLY inspection:
  .\AvCamUdeVmEnumerationReport.ps1

Expected ONLY when USB enumeration and class binding actually succeed:
  VM_UDECX_CONTROLLER_PRESENT
  VM_UDECX_USBVIDEO_PRESENT
  VM_UDECX_ENUMERATION_ONLY_PASSED

Failure: provide the exact PnP status, problem, driver service and
parent property from the report, plus VM guest's
C:\Windows\INF\setupapi.dev.log (only the AvCam-related portions).
These failures are valuable debugging evidence, not proof of success.
If the guest hangs, power it off and restore the HOST-side snapshot.

IMPORTANT:
- Do not remove or touch old AvCamBus driver oem61.inf,
  Logitech C270 USB\VID_046D&PID_0825, or their backups.
- This guest variant is VID_1234&PID_5679 only.
- A successful PnP status is NOT a first Media Foundation video sample.
  The next separate step is runtime USB/UVC transfer investigation;
  native ISO remains disabled intentionally.
- There is no one-click host installer.
