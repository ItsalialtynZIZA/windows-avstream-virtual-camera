#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "../shared/AvCamUsbDescriptors.h"

static std::uint16_t ReadLe16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(p[1] << 8);
}

static std::uint32_t ReadLe32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

static void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main()
{
    try {
        Require(sizeof(g_AvCamUsbDeviceDescriptor) == 18, "Device descriptor must be 18 bytes.");
        Require(g_AvCamUsbDeviceDescriptor[0] == 18, "Device descriptor bLength mismatch.");
        Require(g_AvCamUsbDeviceDescriptor[1] == 0x01, "Device descriptor type mismatch.");
        Require(ReadLe16(&g_AvCamUsbDeviceDescriptor[8]) == AVCAM_USB_VID, "VID mismatch.");
        Require(ReadLe16(&g_AvCamUsbDeviceDescriptor[10]) == AVCAM_USB_PID, "PID mismatch.");

        Require(sizeof(g_AvCamUsbDeviceQualifierDescriptor) == 10, "Qualifier descriptor must be 10 bytes.");
        Require(g_AvCamUsbDeviceQualifierDescriptor[1] == 0x06, "Qualifier descriptor type mismatch.");

        Require(sizeof(g_AvCamUsbConfigurationDescriptor) == 146, "Configuration descriptor byte count mismatch.");
        Require(g_AvCamUsbConfigurationDescriptor[0] == 9, "Configuration bLength mismatch.");
        Require(g_AvCamUsbConfigurationDescriptor[1] == 0x02, "Configuration descriptor type mismatch.");
        Require(ReadLe16(&g_AvCamUsbConfigurationDescriptor[2]) == sizeof(g_AvCamUsbConfigurationDescriptor), "wTotalLength mismatch.");
        Require(g_AvCamUsbConfigurationDescriptor[4] == 2, "Configuration must expose two interfaces.");

        std::size_t offset = 0;
        bool sawIad = false;
        bool sawVc = false;
        bool sawVsAlt0 = false;
        bool sawVsAlt1 = false;
        bool sawMjpegFormat = false;
        bool sawMjpegFrame = false;
        bool sawIsoIn = false;

        while (offset < sizeof(g_AvCamUsbConfigurationDescriptor)) {
            const unsigned char length = g_AvCamUsbConfigurationDescriptor[offset];
            Require(length >= 2, "Descriptor length is invalid.");
            Require(offset + length <= sizeof(g_AvCamUsbConfigurationDescriptor), "Descriptor overruns configuration buffer.");

            const unsigned char type = g_AvCamUsbConfigurationDescriptor[offset + 1];
            if (type == 0x0B) {
                sawIad = true;
                Require(length == 8, "IAD length mismatch.");
            }
            else if (type == 0x04 && length >= 9) {
                const auto interfaceNumber = g_AvCamUsbConfigurationDescriptor[offset + 2];
                const auto alternateSetting = g_AvCamUsbConfigurationDescriptor[offset + 3];
                const auto interfaceClass = g_AvCamUsbConfigurationDescriptor[offset + 5];
                const auto interfaceSubClass = g_AvCamUsbConfigurationDescriptor[offset + 6];
                if (interfaceNumber == 0 && alternateSetting == 0 && interfaceClass == 0x0E && interfaceSubClass == 0x01) {
                    sawVc = true;
                }
                if (interfaceNumber == 1 && alternateSetting == 0 && interfaceClass == 0x0E && interfaceSubClass == 0x02) {
                    sawVsAlt0 = true;
                }
                if (interfaceNumber == 1 && alternateSetting == 1 && interfaceClass == 0x0E && interfaceSubClass == 0x02) {
                    sawVsAlt1 = true;
                }
            }
            else if (type == 0x24 && length >= 3) {
                const auto subtype = g_AvCamUsbConfigurationDescriptor[offset + 2];
                if (subtype == 0x06 && length == 11) {
                    sawMjpegFormat = true;
                }
                if (subtype == 0x07 && length == 30) {
                    sawMjpegFrame = true;
                    Require(ReadLe16(&g_AvCamUsbConfigurationDescriptor[offset + 5]) == AVCAM_UVC_WIDTH, "MJPEG width mismatch.");
                    Require(ReadLe16(&g_AvCamUsbConfigurationDescriptor[offset + 7]) == AVCAM_UVC_HEIGHT, "MJPEG height mismatch.");
                    Require(ReadLe32(&g_AvCamUsbConfigurationDescriptor[offset + 26]) == AVCAM_UVC_FRAME_INTERVAL_100NS, "MJPEG frame interval mismatch.");
                }
            }
            else if (type == 0x05 && length >= 7) {
                const auto endpoint = g_AvCamUsbConfigurationDescriptor[offset + 2];
                const auto attributes = g_AvCamUsbConfigurationDescriptor[offset + 3];
                if (endpoint == 0x81 && (attributes & 0x03) == 0x01) {
                    sawIsoIn = true;
                }
            }

            offset += length;
        }

        Require(offset == sizeof(g_AvCamUsbConfigurationDescriptor), "Descriptor walk did not end at configuration boundary.");
        Require(sawIad, "IAD descriptor not found.");
        Require(sawVc, "VideoControl interface not found.");
        Require(sawVsAlt0, "VideoStreaming alt 0 interface not found.");
        Require(sawVsAlt1, "VideoStreaming alt 1 interface not found.");
        Require(sawMjpegFormat, "MJPEG format descriptor not found.");
        Require(sawMjpegFrame, "MJPEG frame descriptor not found.");
        Require(sawIsoIn, "Isochronous IN endpoint not found.");

        Require(g_AvCamUsbStringLangId[0] == sizeof(g_AvCamUsbStringLangId), "LANGID string length mismatch.");
        Require(g_AvCamUsbStringManufacturer[0] == sizeof(g_AvCamUsbStringManufacturer), "Manufacturer string length mismatch.");
        Require(g_AvCamUsbStringProduct[0] == sizeof(g_AvCamUsbStringProduct), "Product string length mismatch.");
        Require(g_AvCamUsbStringSerial[0] == sizeof(g_AvCamUsbStringSerial), "Serial string length mismatch.");

        std::cout << "Descriptor tests passed.\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
