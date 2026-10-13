#include <ntddk.h>
#include <ks.h>

// M1 device descriptor. Filter factories will be attached in the next step.
// Zero static filters is valid in AVStream and lets us validate driver/PnP
// initialization independently from capture pin implementation.
const KSDEVICE_DESCRIPTOR g_AvCamDeviceDescriptor =
{
    nullptr,                        // Dispatch (AVStream defaults)
    0,                              // FilterDescriptorsCount
    nullptr,                        // FilterDescriptors
    KSDEVICE_DESCRIPTOR_VERSION_2,  // Version
    0,                              // Flags
    nullptr                         // Alignment
};
