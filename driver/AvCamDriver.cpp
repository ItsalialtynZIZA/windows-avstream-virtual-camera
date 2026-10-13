#include <ntddk.h>

// Phase 1 placeholder.
// Next step: convert this into a proper AVStream minidriver and register
// KSDEVICE_DESCRIPTOR / KSFILTER_DESCRIPTOR / KSPIN_DESCRIPTOR_EX.

extern "C"
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrintEx(
        DPFLTR_IHVDRIVER_ID,
        DPFLTR_INFO_LEVEL,
        "AvCam: DriverEntry prototype loaded.\n"
    );

    return STATUS_SUCCESS;
}
