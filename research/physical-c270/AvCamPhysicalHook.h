#pragma once
#include <mfidl.h>

// Hook installed only into the adapted Microsoft sample Device MFT.
// The ETW provider only observes transform state, formats, and fallback causes.
// It does not log frame pixels, biometric data, or camera image content.
void AvCamTrace(const wchar_t* message);
// Nonmatching formats and unavailable OBS data pass through unchanged.
void AvCamSetQueueFormat(const void* queue, IMFMediaType* input, IMFMediaType* output);
void AvCamRemoveQueue(const void* queue);
bool AvCamReplaceMjpegSample(const void* queue, IMFSample* original, IMFSample** replacement);
