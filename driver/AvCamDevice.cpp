#include <ntddk.h>
#include <ks.h>

#include "AvCamFilter.h"

static const KSFILTER_DESCRIPTOR* g_AvCamFilterDescriptors[] =
{
    &g_AvCamFilterDescriptor
};

const KSDEVICE_DESCRIPTOR g_AvCamDeviceDescriptor =
{
    nullptr,                               // Dispatch (AVStream defaults)
    RTL_NUMBER_OF(g_AvCamFilterDescriptors),
    g_AvCamFilterDescriptors,
    KSDEVICE_DESCRIPTOR_VERSION_2,
    0,
    nullptr
};
