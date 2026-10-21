#include "PhysicalEndpointAcceptance.h"
#include <iostream>

using namespace avcam::acceptance;

int main() {
    const std::wstring physical =
        LR"(\\?\USB#VID_046D&PID_0825&MI_00#7&241e5f6c&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\GLOBAL)";
    const std::wstring instance = LR"(USB\VID_046D&PID_0825&MI_00\7&241e5f6c&0&0000)";
    if (!IsExistingPhysicalC270Interface(physical, physical, instance)) return 1;
    if (IsExistingPhysicalC270Interface(physical, L"", instance)) return 2;
    if (IsExistingPhysicalC270Interface(physical, LR"(\\?\SWD#VirtualCamera)", instance)) return 3;
    if (IsExistingPhysicalC270Interface(physical, physical, LR"(SWD\VID_046D&PID_0825)")) return 4;
    if (IsExistingPhysicalC270Interface(physical, physical, LR"(USB\VID_046D&PID_0826)")) return 5;
    if (IsExistingPhysicalC270Interface(L"Logi C270 HD WebCam", L"Logi C270 HD WebCam", instance)) return 8;
    if (IsExistingPhysicalC270Interface(LR"(\\?\SWD#VID_046D&PID_0825)", LR"(\\?\SWD#VID_046D&PID_0825)", instance)) return 9;
    if (IsExistingPhysicalC270Interface(LR"(\\?\ROOT#VID_046D&PID_0825)", LR"(\\?\ROOT#VID_046D&PID_0825)", instance)) return 10;
    if (IsPhysicalLogitechC270Instance(L"USB\\VID_046D&PID_08250\\fake")) return 11;
    if (IsPhysicalLogitechC270InterfacePath(LR"(\\?\USB#VID_046D&PID_08250#fake)")) return 12;
    // A different USB device must not pass by embedding our VID/PID later.
    if (IsPhysicalLogitechC270Instance(
        LR"(USB\VID_1234&PID_5678\VID_046D&PID_0825)")) return 13;
    if (IsPhysicalLogitechC270InterfacePath(
        LR"(\\?\USB#VID_1234&PID_5678#VID_046D&PID_0825#fake)")) return 14;
    if (StrictCaptureAcceptance(true, true, true, false, true, true, true,
                                physical, physical, instance)) return 6;
    if (!StrictCaptureAcceptance(true, true, true, true, true, true, true,
                                 physical, physical, instance)) return 7;
    std::cout << "AVCAM_STRICT_PHYSICAL_ENDPOINT_CONTRACT_PASS\n";
    return 0;
}
