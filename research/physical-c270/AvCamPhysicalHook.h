#pragma once
#include <mfidl.h>

// Hook installed only into the adapted Microsoft sample Device MFT.
// Nonmatching formats and unavailable OBS data pass through unchanged.
void AvCamSetQueueFormat(const void* queue, IMFMediaType* input, IMFMediaType* output);
void AvCamRemoveQueue(const void* queue);
bool AvCamReplaceMjpegSample(const void* queue, IMFSample* original, IMFSample** replacement);
