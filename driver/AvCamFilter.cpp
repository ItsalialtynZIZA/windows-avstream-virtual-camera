#include "AvCamCommon.h"
#include "AvCamFilter.h"
#include "AvCamPin.h"

static const GUID g_AvCamFilterCategories[] =
{
    STATICGUIDOF(KSCATEGORY_VIDEO_CAMERA),
    STATICGUIDOF(KSCATEGORY_VIDEO),
    STATICGUIDOF(KSCATEGORY_CAPTURE)
};

const KSFILTER_DESCRIPTOR g_AvCamFilterDescriptor =
{
    nullptr,
    nullptr,
    KSFILTER_DESCRIPTOR_VERSION,
    0,
    nullptr,
    1,
    sizeof(KSPIN_DESCRIPTOR_EX),
    &g_AvCamCapturePinDescriptor,
    RTL_NUMBER_OF(g_AvCamFilterCategories),
    g_AvCamFilterCategories,
    0,
    0,
    nullptr,
    0,
    nullptr,
    nullptr
};
