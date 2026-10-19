#include "../..//../feeder/src/MediaFoundationDeviceSelector.h"
#include <iostream>
#include <stdexcept>
#include <string>

static void Expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        const std::wstring name = L"Logi C270 HD WebCam";
        const std::wstring link =
            L"\\\\?\\USB#VID_046D&PID_0825&MI_00#7&241E5F6C&0&0000#{camera}";
        Expect(AvCamSourceSelection::IsVidPidPair(L"VID_046D&PID_0825"),
               "Physical C270 VID/PID must parse");
        Expect(AvCamSourceSelection::IsVidPidPair(L"vid_046d&pid_0825"),
               "Lowercase VID/PID must parse");
        Expect(!AvCamSourceSelection::IsVidPidPair(L"VID_046D&PID_08250"),
               "Overlong product ID must be rejected");
        Expect(!AvCamSourceSelection::IsVidPidPair(L"VID_046D&PID_08G5"),
               "Nonhex product ID must be rejected");
        Expect(!AvCamSourceSelection::IsVidPidPair(L"VID_046D"),
               "Vendor-only filter must be rejected");
        Expect(AvCamSourceSelection::MatchesSource(
                   name, link, L"LOGI C270", L"VID_046D&PID_0825"),
               "Logitech physical camera must be identified by BOTH fields");
        Expect(!AvCamSourceSelection::MatchesSource(
                   L"OBS Virtual Camera", link, L"LOGI C270", L"VID_046D&PID_0825"),
               "Wrong display name must not pass");
        Expect(!AvCamSourceSelection::MatchesSource(
                   name, link, name, L"VID_1234&PID_5679"),
               "Physical C270 must not match emulated UdeCx PID");
        Expect(!AvCamSourceSelection::MatchesSource(
                   name, link, name, L"VID_1234&PID_5678"),
               "Physical C270 must not match legacy AvCamBus PID");
        Expect(!AvCamSourceSelection::MatchesSource(
                   name, L"\\\\?\\USB#VID_046D&PID_08250&MI_00#abc",
                   name, L"VID_046D&PID_0825"),
               "Longer PID must not be interpreted as C270");
        Expect(!AvCamSourceSelection::MatchesSource(
                   name, L"", name, L"VID_046D&PID_0825"),
               "Missing symbolic link must fail closed");
        Expect(AvCamSourceSelection::MatchesSource(
                   L"OBS Virtual Camera", L"", L"OBS Virtual Camera", L""),
               "Legacy OBS name-only capture must stay supported");
        std::cout << "C270 Media Foundation source selection passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
