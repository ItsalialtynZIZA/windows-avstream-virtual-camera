#include <ntddk.h>

#define AVCAM_WDM_PROBE_TAG 'PrVA'
#define AVCAM_WDM_PROBE_BUILD_ID 0x20261016u

typedef struct _AVCAM_WDM_PROBE_EXTENSION {
    PDEVICE_OBJECT LowerDevice;
} AVCAM_WDM_PROBE_EXTENSION, *PAVCAM_WDM_PROBE_EXTENSION;

DRIVER_INITIALIZE DriverEntry;
DRIVER_ADD_DEVICE AvCamWdmProbeAddDevice;
DRIVER_UNLOAD AvCamWdmProbeUnload;
_Dispatch_type_(IRP_MJ_PNP) DRIVER_DISPATCH AvCamWdmProbeDispatchPnp;
_Dispatch_type_(IRP_MJ_POWER) DRIVER_DISPATCH AvCamWdmProbeDispatchPower;
DRIVER_DISPATCH AvCamWdmProbeDispatchPassThrough;

static
VOID
AvCamWdmProbeWriteDword(
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PCWSTR ValueName,
    _In_ ULONG Value
    )
{
    OBJECT_ATTRIBUTES attributes;
    UNICODE_STRING valueName;
    HANDLE keyHandle = NULL;
    NTSTATUS status;

    if (RegistryPath == NULL || ValueName == NULL) {
        return;
    }

    InitializeObjectAttributes(
        &attributes,
        RegistryPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
        );

    status = ZwOpenKey(&keyHandle, KEY_SET_VALUE, &attributes);
    if (!NT_SUCCESS(status)) {
        return;
    }

    RtlInitUnicodeString(&valueName, ValueName);
    (VOID)ZwSetValueKey(
        keyHandle,
        &valueName,
        0,
        REG_DWORD,
        &Value,
        sizeof(Value)
        );

    ZwClose(keyHandle);
}

NTSTATUS
AvCamWdmProbeDispatchPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PAVCAM_WDM_PROBE_EXTENSION extension;

    extension = (PAVCAM_WDM_PROBE_EXTENSION)DeviceObject->DeviceExtension;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(extension->LowerDevice, Irp);
}

NTSTATUS
AvCamWdmProbeDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PAVCAM_WDM_PROBE_EXTENSION extension;

    extension = (PAVCAM_WDM_PROBE_EXTENSION)DeviceObject->DeviceExtension;
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(extension->LowerDevice, Irp);
}

NTSTATUS
AvCamWdmProbeDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PIO_STACK_LOCATION stack;
    PAVCAM_WDM_PROBE_EXTENSION extension;

    stack = IoGetCurrentIrpStackLocation(Irp);
    extension = (PAVCAM_WDM_PROBE_EXTENSION)DeviceObject->DeviceExtension;

    if (stack->MinorFunction == IRP_MN_REMOVE_DEVICE) {
        NTSTATUS status;

        IoSkipCurrentIrpStackLocation(Irp);
        status = IoCallDriver(extension->LowerDevice, Irp);
        IoDetachDevice(extension->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(extension->LowerDevice, Irp);
}

NTSTATUS
AvCamWdmProbeAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject
    )
{
    PDEVICE_OBJECT deviceObject = NULL;
    PAVCAM_WDM_PROBE_EXTENSION extension;
    NTSTATUS status;

    status = IoCreateDevice(
        DriverObject,
        sizeof(AVCAM_WDM_PROBE_EXTENSION),
        NULL,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    extension = (PAVCAM_WDM_PROBE_EXTENSION)deviceObject->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));

    extension->LowerDevice = IoAttachDeviceToDeviceStack(
        deviceObject,
        PhysicalDeviceObject
        );
    if (extension->LowerDevice == NULL) {
        IoDeleteDevice(deviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    deviceObject->Flags |= DO_POWER_PAGABLE;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

VOID
AvCamWdmProbeUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    ULONG index;

    AvCamWdmProbeWriteDword(
        RegistryPath,
        L"ProbeBuildId",
        AVCAM_WDM_PROBE_BUILD_ID
        );
    AvCamWdmProbeWriteDword(
        RegistryPath,
        L"ProbeDriverEntryReached",
        1
        );

    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index) {
        DriverObject->MajorFunction[index] = AvCamWdmProbeDispatchPassThrough;
    }

    DriverObject->MajorFunction[IRP_MJ_PNP] = AvCamWdmProbeDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AvCamWdmProbeDispatchPower;
    DriverObject->DriverExtension->AddDevice = AvCamWdmProbeAddDevice;
    DriverObject->DriverUnload = AvCamWdmProbeUnload;

    return STATUS_SUCCESS;
}
