#include <windows.h>
#include <iostream>
#include "VirtualCameraProtocol.h"

int wmain()
{
    std::wcout << L"AvCam feeder prototype\n";
    std::wcout << L"Protocol version: " << AVCAM_PROTOCOL_VERSION << L"\n";
    std::wcout << L"Next milestone: enumerate the driver interface and submit frames.\n";
    return 0;
}
