#pragma once
// Compile-time invariant: a shared physical camera MFT must not substitute
// OBS frames for every client. CMake-only checks cannot inspect all /D flags.
#if defined(AVCAM_EXPERIMENTAL_UNSCOPED_MFT_REPLACEMENT)
#error "Unscoped OBS replacement is prohibited: risks Deep Live Cam feedback in multi-app mode"
#endif
