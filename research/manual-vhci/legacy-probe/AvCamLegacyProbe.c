#include <ntddk.h>

#define AVCAM_LEGACY_PROBE_BUILD_ID 0x20261016u
#define AVCAM_LEGACY_PROBE_ENTRY_MARKER 0xA11C1001u

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD AvCamLegacyProbeUnload;

static VOID
AvCamLegacyProbeWriteDword(
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PCWSTR ValueName,
    _In_ ULONG Value
    )
{
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING valueName;
    HANDLE keyHandle = NULL;
    NTSTATUS status;

    if (RegistryPath == NULL || ValueName == NULL) {
        return;
    }

    InitializeObjectAttributes(
        &objectAttributes,
        RegistryPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    status = ZwOpenKey(&keyHandle, KEY_SET_VALUE, &objectAttributes);
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
        sizeof(Value));

    ZwClose(keyHandle);
}

VOID
AvCamLegacyProbeUnload(
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
    AvCamLegacyProbeWriteDword(
        RegistryPath,
        L"ProbeBuildId",
        AVCAM_LEGACY_PROBE_BUILD_ID);

    AvCamLegacyProbeWriteDword(
        RegistryPath,
        L"ProbeDriverEntryReached",
        AVCAM_LEGACY_PROBE_ENTRY_MARKER);

    DriverObject->DriverUnload = AvCamLegacyProbeUnload;
    return STATUS_SUCCESS;
}
