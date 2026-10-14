# Production signing path

The current CI output is a **test-signed development package**. It is suitable for controlled development and runtime validation, not broad retail deployment.

## Current development state

The repository currently produces:

- `AvCam.sys`
- `AvCam.inf`
- `AvCam.cat`
- a CI test certificate
- installation and verification helpers

The development package is intentionally kept separate from a production signing workflow.

## Recommended release path

For a production Windows 11 x64 release, use Microsoft's Hardware Dev Center signing flow.

### 1. Complete runtime validation first

Before submitting anything for production signing, complete the runtime gates in `docs/hardening.md`:

- install/enumeration on Windows 11 x64;
- Windows Camera capture;
- feeder transport;
- OBS bridge;
- browser capture;
- reconnect/open-close stress;
- Driver Verifier pass;
- pool/IRQL/concurrency validation.

Do not treat a clean compile or PREfast pass as a substitute for runtime driver validation.

### 2. Register for the Windows Hardware Developer Program

Microsoft requires a Hardware Dev Center / Partner Center account for modern kernel-mode driver signing submissions.

The organization account must have an appropriate code-signing certificate associated with it. Microsoft currently documents an EV certificate requirement for establishing/submitting through the hardware dashboard.

### 3. Choose the signing route

#### HLK / Windows Hardware Compatibility Program

This is the preferred release route for broadly distributed production drivers.

Typical flow:

1. Prepare the final unsigned/release package.
2. Run the applicable Windows Hardware Lab Kit tests.
3. Produce the HLK submission package.
4. Submit it through Hardware Dev Center.
5. Download the Microsoft-signed result.
6. Verify the returned catalog/binaries with SignTool.
7. Test the exact signed package again on clean Windows 11 systems.

HLK/dashboard signing is the appropriate path when the project is intended for normal public distribution and Windows Update scenarios.

#### Attestation signing

Microsoft also provides attestation signing for supported Windows client driver scenarios. It does not require HLK testing, but Microsoft explicitly positions it as a more limited route and it does not make the package Windows Certified.

Do not assume attestation signing is suitable for every final distribution target. Re-check Microsoft's current Hardware Dev Center requirements before release.

### 4. Keep CI test signing separate

Production CI must not reuse the repository's development test certificate.

Recommended separation:

```text
Development build
  -> CI test certificate
  -> dedicated test VM/machine

Release candidate
  -> runtime/Verifier validation
  -> Hardware Dev Center submission
  -> Microsoft-signed package
  -> clean-machine validation
```

Never commit production private keys, PFX files, certificate passwords, Partner Center credentials, or signing tokens to the repository.

### 5. Release verification

For the Microsoft-signed output, verify at minimum:

- catalog signature is valid;
- `AvCam.sys` matches the submitted package;
- INF references the expected catalog;
- clean Windows 11 installation succeeds with normal driver-signing enforcement;
- Secure Boot remains enabled for production validation;
- no test certificate is required;
- no `TESTSIGNING` boot configuration is required;
- Device Manager reports the driver normally;
- Windows Camera / browser / OBS bridge behavior is unchanged from the validated development build.

### 6. Distribution boundary

This project is an AVStream virtual camera. Production metadata and signing must describe the software truthfully. Do not add fake USB VID/PID, USB bus speed, UVC descriptors, or physical-device claims to make the driver appear to be hardware that it is not.

## References

Use the current Microsoft Learn documentation before a release submission:

- Driver signing options and best practices
- Partner Center for Windows Hardware
- Driver code-signing requirements
- Kernel-mode code-signing requirements
- Windows driver signing tutorial

The exact Partner Center workflow and certificate requirements can change, so release engineering should verify the current Microsoft documentation at submission time.
