#include <ntddk.h>
#include <wdf.h>
#include <udecx.h>

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AvUdeEvtDeviceAdd;
EVT_UDECX_WDF_DEVICE_QUERY_USB_CAPABILITY AvUdeEvtQueryUsbCapability;

extern "C"
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, AvUdeEvtDeviceAdd);

    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);
}

NTSTATUS AvUdeEvtQueryUsbCapability(
    _In_ WDFDEVICE UdecxWdfDevice,
    _In_ PGUID CapabilityType,
    _In_ ULONG OutputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _Out_ PULONG ResultLength)
{
    UNREFERENCED_PARAMETER(UdecxWdfDevice);
    UNREFERENCED_PARAMETER(CapabilityType);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);

    if (ResultLength != nullptr)
    {
        *ResultLength = 0;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS AvUdeEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    NTSTATUS status = UdecxInitializeWdfDeviceInit(DeviceInit);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDFDEVICE device = nullptr;
    status = WdfDeviceCreate(&DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    UDECX_WDF_DEVICE_CONFIG udecxConfig;
    UDECX_WDF_DEVICE_CONFIG_INIT(&udecxConfig, AvUdeEvtQueryUsbCapability);
    udecxConfig.NumberOfUsb20Ports = 1;
    udecxConfig.NumberOfUsb30Ports = 0;

    status = UdecxWdfDeviceAddUsbDeviceEmulation(device, &udecxConfig);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    // Phase 1 deliberately stops at a working emulated host controller.
    // The USB/UVC child device is created in the next milestone so that
    // controller startup and child enumeration failures can be isolated.
    return STATUS_SUCCESS;
}
