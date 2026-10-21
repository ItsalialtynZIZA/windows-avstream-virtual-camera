# OBS → AvCam IPC: live-source smoke test (no driver changes)

This is a **diagnostic**, not a working physical Logitech camera replacement.
It reads OBS Studio's `OBSVirtualCamVideo` shared-memory queue, converts NV12 to
1280x720 BGRA using the existing `ObsSharedMemorySource`, and publishes up to
90 complete frames into a **per-user** `Local\\AvCamFullFrameMockV1` mapping.
Two child processes independently copy frames and verify checksum and timestamp
monotonicity. It does not open C270 or register/modify any driver or Device MFT.

## Running on Windows

1. Start OBS Studio, configure a moving video source and **Start Virtual Camera**.
2. Download the latest successful `AvCam-Physical-C270-RC3` CI artifact *if*
   the artifact includes `AvCamMockFrameIpc.exe`; otherwise build locally from
   `research/physical-c270/mock-fanout` using CMake and Visual Studio.
3. In a normal PowerShell console in the build output folder run
   `./AvCamMockFrameIpc.exe --obs`. No Administrator rights are needed.
4. Check `IPC_FRAME_READER` and `AVCAM_IPC_FULL_FRAME_PASS`. Report all
   messages and exit codes if it fails.

To test synthetic frames without OBS, run `./AvCamMockFrameIpc.exe`.
Without OBS running, `--obs` should fail explicitly with
`OBS_SOURCE_START_FAILED` and exit code 8; it never silently substitutes
test-generated frames.

**Limits:** this is single-producer, latest-frame IPC with frame skips.
The OBS source and reader are still experimental; live OBS hardware results
have not been confirmed. Neither a passing CI run nor this smoke test proves
that unmodified Deep Live Cam and Chrome can simultaneously open the existing
physical C270. That physical camera sharing problem remains unresolved.
