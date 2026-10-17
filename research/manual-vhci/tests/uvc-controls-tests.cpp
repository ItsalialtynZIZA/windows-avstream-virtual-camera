#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "../shared/AvCamUvcControls.h"

static void Check(bool valid, const char* error)
{
    if (!valid) {
        throw std::runtime_error(error);
    }
}

static int Query(
    AVCAM_UVC_STREAM_STATE& state,
    unsigned char request,
    unsigned char selector,
    bool in,
    unsigned char* buffer,
    unsigned int length,
    unsigned int& actual
    )
{
    return AvCamUvcProcessClassInterface(
        &state, request, static_cast<unsigned short>(selector << 8), 1,
        in ? 1 : 0, buffer, length, &actual);
}

int main()
{
    try {
        AVCAM_UVC_STREAM_STATE state{};
        AvCamUvcInitState(&state);

        // The control model must match the MJPEG frame descriptor exactly.
        const auto* configuration = g_AvCamUsbConfigurationDescriptor;
        Check(AVCAM_UVC_FRAME_MAX_BYTES == 0x001F9000u,
              "MJPEG max frame size mismatch.");
        bool foundFrame = false;
        for (std::size_t pos = 0; pos < sizeof(g_AvCamUsbConfigurationDescriptor);) {
            const auto length = configuration[pos];
            Check(length >= 2 && pos + length <= sizeof(g_AvCamUsbConfigurationDescriptor),
                  "Malformed configuration descriptor.");
            if (length == 30 && configuration[pos + 1] == 0x24 &&
                configuration[pos + 2] == 0x07) {
                Check(AvCamUvcReadLe32(configuration + pos + 17) ==
                      AVCAM_UVC_FRAME_MAX_BYTES,
                      "Max frame bytes must equal the frame descriptor.");
                foundFrame = true;
            }
            pos += length;
        }
        Check(foundFrame, "MJPEG frame descriptor absent.");

        unsigned char buffer[AVCAM_UVC_CONTROL_LENGTH]{};
        unsigned int actual = 0;
        auto rc = Query(state, AVCAM_UVC_GET_LEN, AVCAM_UVC_VS_PROBE_CONTROL,
                        true, buffer, 2, actual);
        Check(rc == AVCAM_UVC_RESULT_OK && actual == 2 &&
              buffer[0] == 34 && buffer[1] == 0, "GET_LEN probe failed.");

        rc = Query(state, AVCAM_UVC_GET_INFO, AVCAM_UVC_VS_COMMIT_CONTROL,
                   true, buffer, 1, actual);
        Check(rc == AVCAM_UVC_RESULT_OK && actual == 1 && buffer[0] == 3,
              "GET_INFO commit failed.");

        rc = Query(state, AVCAM_UVC_GET_DEF, AVCAM_UVC_VS_PROBE_CONTROL,
                   true, buffer, 34, actual);
        Check(rc == AVCAM_UVC_RESULT_OK && actual == 34 &&
              buffer[2] == 1 && buffer[3] == 1 &&
              AvCamUvcReadLe32(buffer + 4) == AVCAM_UVC_FRAME_INTERVAL_100NS &&
              AvCamUvcReadLe32(buffer + 18) == AVCAM_UVC_FRAME_MAX_BYTES &&
              AvCamUvcReadLe32(buffer + 22) == AVCAM_UVC_PAYLOAD_MAX_BYTES,
              "GET_DEF probe differs from advertised format.");

        rc = Query(state, AVCAM_UVC_SET_CUR, AVCAM_UVC_VS_PROBE_CONTROL,
                   false, buffer, 34, actual);
        Check(rc == AVCAM_UVC_RESULT_OK && actual == 34, "SET_CUR probe failed.");

        buffer[22] = 1; buffer[23] = 0; buffer[24] = 0; buffer[25] = 0;
        rc = Query(state, AVCAM_UVC_SET_CUR, AVCAM_UVC_VS_COMMIT_CONTROL,
                   false, buffer, 34, actual);
        Check(rc == AVCAM_UVC_RESULT_OK && state.Committed == 1, "Commit failed.");

        std::memset(buffer, 0, sizeof(buffer));
        rc = Query(state, AVCAM_UVC_GET_CUR, AVCAM_UVC_VS_COMMIT_CONTROL,
                   true, buffer, 34, actual);
        Check(rc == AVCAM_UVC_RESULT_OK && actual == 34 &&
              AvCamUvcReadLe32(buffer + 22) == AVCAM_UVC_PAYLOAD_MAX_BYTES,
              "GET_CUR commit must report negotiated payload size.");

        buffer[2] = 2; // unsupported format
        rc = Query(state, AVCAM_UVC_SET_CUR, AVCAM_UVC_VS_PROBE_CONTROL,
                   false, buffer, 34, actual);
        Check(rc == AVCAM_UVC_RESULT_STALL, "Unsupported format should stall.");

        unsigned int ignored = 123;
        rc = AvCamUvcProcessClassInterface(
            &state, AVCAM_UVC_GET_CUR, 0x0300, 1, 1, buffer, 34, &ignored);
        Check(rc == AVCAM_UVC_RESULT_STALL && ignored == 0,
              "Unsupported selector should stall.");

        rc = AvCamUvcProcessClassInterface(
            &state, AVCAM_UVC_GET_CUR, 0x0100, 0, 1, buffer, 34, &ignored);
        Check(rc == AVCAM_UVC_RESULT_STALL, "Wrong interface should stall.");

        rc = Query(state, AVCAM_UVC_GET_DEF, AVCAM_UVC_VS_COMMIT_CONTROL,
                   true, buffer, 34, actual);
        Check(rc == AVCAM_UVC_RESULT_STALL, "Unsupported GET_DEF(COMMIT) should stall.");

        rc = Query(state, AVCAM_UVC_GET_CUR, AVCAM_UVC_VS_PROBE_CONTROL,
                   true, buffer, 2, actual);
        Check(rc == AVCAM_UVC_RESULT_OK && actual == 2, "Short GET_CUR must not overflow.");

        rc = Query(state, AVCAM_UVC_SET_CUR, AVCAM_UVC_VS_PROBE_CONTROL,
                   false, buffer, 26, actual);
        Check(rc == AVCAM_UVC_RESULT_INVALID, "Short SET_CUR must fail safely.");

        std::cout << "UVC Probe/Commit contract tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
