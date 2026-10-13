#include <ntddk.h>
#include <ks.h>
#include <ksmedia.h>

#include "AvCamFilter.h"

// Minimal camera filter factory. Capture pins will be added in the next milestone.
// Keeping the filter pin-less for this commit lets us validate AVStream filter
// registration separately from media format negotiation and streaming.
static const GUID g_AvCamFilterCategories[] =
{
    STATICGUIDOF(KSCATEGORY_VIDEO_CAMERA),
    STATICGUIDOF(KSCATEGORY_VIDEO),
    STATICGUIDOF(KSCATEGORY_CAPTURE)
};

const KSFILTER_DESCRIPTOR g_AvCamFilterDescriptor =
{
    nullptr,                                // Dispatch
    nullptr,                                // AutomationTable
    KSFILTER_DESCRIPTOR_VERSION,            // Version
    0,                                      // Flags
    nullptr,                                // ReferenceGuid
    0,                                      // PinDescriptorsCount
    0,                                      // PinDescriptorSize
    nullptr,                                // PinDescriptors
    RTL_NUMBER_OF(g_AvCamFilterCategories), // CategoriesCount
    g_AvCamFilterCategories,                // Categories
    0,                                      // NodeDescriptorsCount
    0,                                      // NodeDescriptorSize
    nullptr,                                // NodeDescriptors
    0,                                      // ConnectionsCount
    nullptr,                                // Connections
    nullptr                                 // ComponentId
};
