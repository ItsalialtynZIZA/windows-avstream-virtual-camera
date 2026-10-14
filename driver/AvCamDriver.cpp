#include "AvCamCommon.h"
#include "AvCamDevice.h"

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C"
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    return KsInitializeDriver(
        DriverObject,
        RegistryPath,
        &g_AvCamDeviceDescriptor
    );
}
