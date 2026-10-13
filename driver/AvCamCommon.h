#pragma once

extern "C" {
#include <wdm.h>
}

#include <windef.h>
#include <unknown.h>
#include <ks.h>
#include <ksmedia.h>

// RGB32 is used by Microsoft's AVStream camera sample but is not declared by
// every WDK header combination. Keep the canonical DirectShow/KS subtype GUID
// local to this project so the driver builds consistently with the pinned WDK.
#ifndef STATIC_KSDATAFORMAT_SUBTYPE_RGB32
#define STATIC_KSDATAFORMAT_SUBTYPE_RGB32 \
    0xe436eb7e, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70
DEFINE_GUIDSTRUCT("e436eb7e-524f-11ce-9f53-0020af0ba770", KSDATAFORMAT_SUBTYPE_RGB32);
#define KSDATAFORMAT_SUBTYPE_RGB32 DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_RGB32)
#endif
