# Research decision: post-fanout per-client capture effects

Date: 2026-10-22
Host baseline: Windows 11 Pro 25H2 build 26200.9711, real Logitech
C270, multi-app mode enabled. Owner observed unchanged Deep Live Cam
and Chrome simultaneously opening the same physical C270.

## Critical distinction
Microsoft documentation separates:
- Device MFT: a *per-device* extension, processing camera frames before
  application distribution. It is the current AvCam sample adaptation.
  https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/dmft-design
- Legacy driver MFT (MFT0): a transform that can be instantiated per
  stream/pipeline, not equivalent to Device MFT.
  https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/creating-a-camera-driver-mft
- The reference sample distinguishes a DeviceMFT instance attached to
  the source from MFT0 instances used per stream.
  https://github.com/microsoft/Windows-driver-samples/blob/main/avstream/sampledevicemft/README.md

Windows 11 Multi-app camera permits multiple applications to share
camera capture. The supported feature does NOT document a user-defined
post-fanout transformation for an unmodified Chrome getUserMedia client:
https://support.microsoft.com/en-us/windows/hardware/camera/manage-cameras-with-camera-settings-in-windows-11

## Strict contract
Keep original USB VID_046D PID_0825 camera endpoint. Deep Live Cam
receives raw frames; Chrome receives OBS-processed frames; no recursion;
OBS stop restores raw frames; no extra software camera endpoints.

## Investigations that are NOT sufficient
- A process-global Device MFT on the original sensor cannot be treated
  as a Chrome-only effect.
- An MFT0 instance per stream does not prove the transform knows which
  app requested its frames, nor that Chrome's MF capture uses that path.
- Frame size, MJPEG negotiation, source PID, FrameServer process identity,
  startup order and friendly name are not reliable client identity.
- Virtual camera endpoints, software camera GUIDs and USB display-name
  aliases violate the strict requirement.

## Concrete next decision gate, without changing owner's camera
Find a documented API that BOTH:
  1. runs a custom frame transform *after* Windows forks the physical
     C270 stream for an unmodified Chrome getUserMedia consumer, AND
  2. supplies an authenticated per-client identity or allows a
     Chrome-specific opt-in without modifying Chrome or Deep Live Cam.

If no such API is established, maintain status BLOCKED; do not infer
that a low-level driver replacement can satisfy this contract. The
existing read-only diagnostics and the working 25H2 multi-app baseline
are preserved.

## Current implementation status
OBS to AvCam frame IPC works (~30fps on the user's PC); strict
Chrome-only substitution of this stream is NOT implemented.
Device MFT OBS injection is disabled in distributed builds.
