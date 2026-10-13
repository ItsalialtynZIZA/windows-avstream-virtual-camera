#include "AvCamCommon.h"
#include "AvCamFilter.h"
#include "AvCamPin.h"

static const GUID g_AvCamFilterCategories[] =
{
    STATICGUIDOF(KSCATEGORY_VIDEO_CAMERA),
    STATICGUIDOF(KSCATEGORY_VIDEO),
    STATICGUIDOF(KSCATEGORY_CAPTURE)
};

static const KSPIN_DESCRIPTOR_EX g_AvCamPins[] =
{
    g_AvCamCapturePinDescriptor
};

const KSFILTER_DESCRIPTOR g_AvCamFilterDescriptor =
{
    nullptr,
    nullptr,
    KSFILTER_DESCRIPTOR_VERSION,
    0,
    nullptr,
    RTL_NUMBER_OF(g_AvCamPins),
    sizeof(KSPIN_DESCRIPTOR_EX),
    g_AvCamPins,
    RTL_NUMBER_OF(g_AvCamFilterCategories),
    g_AvCamFilterCategories,
    0,
    0,
    nullptr,
    0,
    nullptr,
    nullptr
};
