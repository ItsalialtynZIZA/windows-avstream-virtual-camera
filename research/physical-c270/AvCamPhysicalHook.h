#pragma once
#include <mfidl.h>

// Hook installed only into the adapted Microsoft sample Device MFT.
// The ETW provider only observes transform state, formats, and fallback causes.
// It does not log frame pixels, biometric data, or camera image content.
void AvCamTrace(const wchar_t* message);
// Log only upstream and own Device MFT pin counts; never log frames, paths or client IDs.
// Upstream may be DevProxy OR an OEM DMFT; identity remains unverified.
void AvCamTraceDeviceMftTopology(long result, unsigned long upstreamInput,
                                 unsigned long upstreamOutput,
                                 unsigned long ownInput, unsigned long ownOutput);
// Enumerate ONLY advertised types on the actual connected upstream transform.
// Never logs pixels, activates a camera, or asserts that upstream is Sonix.
// Called only after the reference MFT obtained upstream stream IDs.
void AvCamTraceUpstreamMediaContract(IMFDeviceTransform* upstream,
                                     const unsigned long* outputIds,
                                     unsigned long outputCount);
// Nonmatching formats and unavailable OBS data pass through unchanged.
void AvCamSetQueueFormat(const void* queue, IMFMediaType* input, IMFMediaType* output);
void AvCamRemoveQueue(const void* queue);
bool AvCamReplaceMjpegSample(const void* queue, IMFSample* original, IMFSample** replacement);
