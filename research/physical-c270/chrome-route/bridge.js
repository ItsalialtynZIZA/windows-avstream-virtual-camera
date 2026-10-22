/*
 * AvCam Chrome-only capture router. Runs in the site's MAIN JS world on
 * specifically authorized domains. This is NOT a physical USB device,
 * Windows driver, or genuine Logitech identity override.
 *
 * Does not read/modify microphone input, USB VID/PID, enumerateDevices,
 * MediaStreamTrack.getSettings, or any native camera driver.
 */
(() => {
  "use strict";

  const mediaDevices = navigator.mediaDevices;
  if (!mediaDevices ||
      typeof mediaDevices.getUserMedia !== "function" ||
      typeof mediaDevices.enumerateDevices !== "function") return;

  const installed = Symbol.for("avcam.chromeRoute.v1");
  if (mediaDevices[installed]) return;

  const nativeGetUserMedia = mediaDevices.getUserMedia.bind(mediaDevices);
  const nativeEnumerate = mediaDevices.enumerateDevices.bind(mediaDevices);
  const isLogitech = (device) =>
    /(?:^|\W)(?:logitech|logi|c270)(?:\W|$)/i.test(device.label || "");
  const isObs = (device) =>
    /\bobs\b.*\bvirtual\b.*\bcamera\b/i.test(device.label || "");

  function selectedIds(video) {
    if (!video || typeof video !== "object") return [];
    const value = video.deviceId;
    if (typeof value === "string") return [value];
    if (!value || typeof value !== "object") return [];
    const ids = [...[].concat(value.exact || []), ...[].concat(value.ideal || [])];
    return ids.filter((id) => typeof id === "string");
  }

  const routedGetUserMedia = async function getUserMedia(constraints) {
    if (!constraints || typeof constraints !== "object" || !constraints.video) {
      return nativeGetUserMedia(constraints);
    }

    const devices = await nativeEnumerate();
    const cameras = devices.filter((d) => d.kind === "videoinput");
    if (cameras.length === 0 || cameras.some((d) => !d.label)) {
      // Browser privacy rules hide labels until the site has camera consent.
      // Do not silently open the physical camera in an enabled site.
      throw new DOMException(
        "AvCam: camera labels are unavailable. Grant the site camera access in Chrome site settings and reload.",
        "NotAllowedError"
      );
    }

    const obs = cameras.find(isObs);
    const requestedIds = selectedIds(constraints.video);
    const explicit = requestedIds.length > 0;
    const selected = explicit
      ? cameras.find((d) => requestedIds.includes(d.deviceId)) ||
        (requestedIds.includes("default") ? cameras[0] : null)
      : cameras[0]; // enumerateDevices lists the browser's default first.

    // Pass through unrelated physical cameras and deliberate OBS selections.
    // We never pretend that the OBS stream has Logitech hardware identifiers.
    if (!selected || !isLogitech(selected)) {
      return nativeGetUserMedia(constraints);
    }

    if (!obs || !obs.deviceId) {
      throw new DOMException(
        "AvCam: OBS Virtual Camera is not available. Start OBS Virtual Camera.",
        "NotFoundError"
      );
    }

    const originalVideo =
      constraints.video === true ? {} : {...constraints.video};
    const routedVideo = {
      ...originalVideo,
      deviceId: {exact: obs.deviceId}
    };
    // Constraints aimed at a physical camera must not force OBS hardware
    // controls. Keep width/height/frameRate/audio from the calling site.
    for (const key of ["advanced", "groupId", "facingMode",
                       "pan", "tilt", "zoom"]) {
      delete routedVideo[key];
    }

    const routed = {...constraints, video: routedVideo};
    console.info("[AvCam] Logitech capture request routed to OBS Virtual Camera on this permitted site.");
    return nativeGetUserMedia(routed);
  };

  Object.defineProperty(mediaDevices, "getUserMedia", {
    value: routedGetUserMedia,
    configurable: true,
    writable: true
  });
  Object.defineProperty(mediaDevices, installed, {
    value: true,
    configurable: false
  });
})();
