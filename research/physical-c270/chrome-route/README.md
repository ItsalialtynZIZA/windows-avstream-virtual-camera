# AvCam Chrome Route — Chrome-only experimental MVP

**Scope:** On explicitly enabled sites, route JavaScript MediaDevices.getUserMedia
requests for **Logitech C270** (as selected in Chrome's normal camera UI)
to the ordinary **OBS Virtual Camera** capture endpoint. Leave other sites,
microphones and the Logitech device driver untouched. Deep Live Cam continues
to read the physical Logitech directly, as before.

## Hard limitations

- This is a consent-based **web API-level substitution**, NOT a USB/UVC
  identity replacement. Chrome settings and enumerateDevices() still show
  the physical Logitech. The returned video track reports the real
  OBS Virtual Camera label and deviceId. Websites examining the actual
  capture track can distinguish it. No fake VID/PID or native identity
  is fabricated.
- Only websites using the JavaScript getUserMedia API after the MAIN-world
  bridge is installed are covered. Native Windows clients, Chrome-internal
  pages, protected cross-origin frames and sites using other capture paths
  are not covered.
- The target website must already have granted Chrome camera permission,
  so enumerateDevices() reveals camera labels. When labels are hidden,
  the router **fails closed** (NotAllowedError) instead of opening the
  physical Logitech in contention with Deep Live Cam.
- Chrome host permissions ignore port numbers. Approving
  http://localhost:8765 also covers other ports at http://localhost.
  Disable the extension after experiments.
- Stability on the user's Windows host remains unverified. The solution
  deliberately avoids the experimental AvCam COM DLL previously associated
  with a Frame Server hang.

## Preparation, activation and rollback

1. **Quarantine/remove the old experimental Windows Media Foundation source.**
   From administrator PowerShell, run:
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File
   'C:\AvCam-New\Manage-AvCamMfSource.ps1' -Action Unregister
   -AcceptExperimental

   Run the same script with -Action Status. Registration must say ABSENT.
   If the DLL is locked, a normal Windows restart may be required.
   **Do not retry** AvCamSoftwareCameraControl.exe --session.

2. Start the physical Logitech C270 in Deep Live Cam. Start OBS and enable
   **OBS Virtual Camera**; ensure OBS preview displays the desired processed
   feed. You do NOT need AvCam-Start.cmd, the IPC pipe service, or a new
   Windows camera driver for this Chrome-only method.

3. Chrome -> chrome://extensions -> Developer mode -> Load unpacked ->
   choose the directory containing this manifest.json.

4. At chrome://settings/content/camera set the camera to the *physical*
   Logitech C270. The OBS Virtual Camera must also remain installed and
   discoverable, but not selected as Chrome's default device.

5. Visit an HTTPS getUserMedia camera test site (or a localhost test site).
   In Chrome's site settings, grant **Camera: Allow** for this site before
   activating AvCam, then reload. Camera labels must be exposed for that
   site's origin. Without prior permission this version intentionally
   refuses to capture; it does not silently fall back to RAW.

6. Open the AvCam extension popup on the test page. Press
   **Направить Logitech → OBS на этом сайте** and approve host permission.
   The current page reloads.

7. Initiate video capture on the site, selecting **Logitech C270**
   or the Chrome default Logitech camera. Chrome's JS capture request
   is routed to the native OBS Virtual Camera, so processed frames should
   appear. Developer console logs '[AvCam] Logitech capture request routed'.
   The track label correctly reports OBS; that is intentional.

8. Check that Deep Live Cam still shows original RAW and OBS preview
   remains processed. If the browser shows black frames, stop the test.
   Diagnose whether the same site can display OBS Virtual Camera directly
   with the extension disabled; do not reopen the old FrameServer source.

9. To disable: open the popup -> **Отключить маршрутизацию**. The page
   reloads and restores the native browser getUserMedia function.
   You may also disable or uninstall the entire extension in
   chrome://extensions.

## Security and engineering scope

- Per-domain explicit opt-in: the extension asks Chrome for permission
  to the chosen site, not every website by default.
- No network server, driver install, COM registration, Logitech
  firmware changes, registry changes, or browser process injection.
- Pixels are delivered directly by native OBS Virtual Camera to the
  chosen site; the extension does not transmit video anywhere.
- Do not use the bridge to misrepresent a physical camera in identity
  verification or other authenticity checks. It cannot replicate real
  hardware identity or prove that an image originated from Logitech.

## Automated tests

Run from the repository root:

    node --test research/physical-c270/chrome-route/tests/bridge.test.cjs

No npm dependencies. Tests cover routing, audio preservation,
unrelated-device pass-through, fail-closed permission handling,
identity transparency and idempotent setup.

A passing Node test is not a real Windows/Chrome test.
