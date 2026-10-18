/*
 * Prepared ISO URB adapter for the UdeCx research endpoint.
 *
 * This module deliberately does not queue or complete a WDFREQUEST.
 * Its caller must already own the request in a PASSIVE_LEVEL worker,
 * hold a stable immutable MJPEG frame, and serialize the ISO stream state.
 * Never call this helper from a DPC or after a cancellation completion.
 */
#include "AvCamUdeUsb.h"
#include "AvCamUdeIso.h"
#include "AvCamUdeFramePump.h"
#include "AvCamUdeIsoUrbLayout.h"

#define AVCAM_UDE_ISO_WORKSPACE_TAG 'sIcA'

static NTSTATUS
AvCamUdePrepareIsoUrbCore(
    _In_ WDFREQUEST request,
    _Inout_opt_ AVCAM_UDE_ISO_STREAM* stream,
    _In_reads_bytes_opt_(frameBytes) const UCHAR* frame,
    _In_ ULONG frameBytes,
    _Inout_opt_ AVCAM_UDE_FRAME_PUMP* pump,
    _In_ ULONGLONG firstMicroframe100ns,
    _Out_ PULONG bytesFilled)
{
    WDF_REQUEST_PARAMETERS params;
    PURB urb;
    PUCHAR transfer = NULL;
    ULONG mappedBytes = 0;
    ULONG transferBytes;
    ULONG packets;
    ULONG i;
    unsigned int total = 0;
    unsigned int* workspace;
    unsigned int* offsets;
    unsigned int* lengths;
    SIZE_T workspaceBytes;
    NTSTATUS status;
    int result;

    if (bytesFilled == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *bytesFilled = 0;
    if (pump == NULL) {
        if (stream == NULL || frame == NULL ||
            frameBytes < 4 || frameBytes > AVCAM_UVC_FRAME_MAX_BYTES ||
            frame[0] != 0xFF || frame[1] != 0xD8 ||
            frame[frameBytes - 2] != 0xFF ||
            frame[frameBytes - 1] != 0xD9) {
            return STATUS_INVALID_PARAMETER;
        }
    } else if (stream != NULL || frame != NULL ||
               frameBytes != 0 || pump->Source == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Validate the host's variable-length descriptors BEFORE accessing them.
     * The same request must remain owned by the worker until its completion.
     */
    status = AvCamUdeInspectIsoUrb(request);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(request, &params);
    urb = (PURB)params.Parameters.Others.Arg1;
    if (urb == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    packets = urb->UrbIsochronousTransfer.NumberOfPackets;
    transferBytes = urb->UrbIsochronousTransfer.TransferBufferLength;

    status = UdecxUrbRetrieveBuffer(request, &transfer, &mappedBytes);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (transfer == NULL || transferBytes > mappedBytes ||
        packets == 0 || packets > AVCAM_UDE_ISO_MAX_PACKETS) {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    /* Allocate bounded workspace instead of placing 8 KiB on the kernel stack.
     * Packet windows are host-defined; do not rewrite their offsets.
     */
    workspaceBytes = (SIZE_T)packets * 2u * sizeof(unsigned int);
    workspace = (unsigned int*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, workspaceBytes, AVCAM_UDE_ISO_WORKSPACE_TAG);
    if (workspace == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    offsets = workspace;
    lengths = workspace + packets;
    for (i = 0; i < packets; ++i) {
        offsets[i] = urb->UrbIsochronousTransfer.IsoPacket[i].Offset;
        lengths[i] = 0;
    }

    /* The portable encoder stages its stream state and validates every
     * window before changing the stream. All per-packet metadata is written
     * only after successful encoding.
     */
    if (pump == NULL) {
        result = AvCamUdeIsoFill(
            stream, frame, frameBytes, transfer, transferBytes,
            offsets, packets, firstMicroframe100ns, lengths, &total);
        if (result != AVCAM_UDE_ISO_OK) {
            status = result == AVCAM_UDE_ISO_INVALID ?
                STATUS_INVALID_PARAMETER : STATUS_DATA_ERROR;
            goto Exit;
        }
    } else {
        result = AvCamUdeFramePumpFill(
            pump, transfer, transferBytes, offsets, packets,
            firstMicroframe100ns, lengths, &total);
        if (result != AVCAM_UDE_PUMP_OK) {
            status = result == AVCAM_UDE_PUMP_NO_FRAME ?
                STATUS_DEVICE_NOT_READY : STATUS_INVALID_PARAMETER;
            goto Exit;
        }
    }

    /* A zero-length scheduled microframe is still a successful ISO packet.
     * Lengths must stay within their original sparse host windows.
     */
    for (i = 0; i < packets; ++i) {
        urb->UrbIsochronousTransfer.IsoPacket[i].Length = lengths[i];
        urb->UrbIsochronousTransfer.IsoPacket[i].Status = USBD_STATUS_SUCCESS;
    }
    urb->UrbIsochronousTransfer.ErrorCount = 0;
    *bytesFilled = total;
    status = STATUS_SUCCESS;

Exit:
    ExFreePoolWithTag(workspace, AVCAM_UDE_ISO_WORKSPACE_TAG);
    return status;
}

/* Legacy single-frame staging entry point. Still not called from EP 0x81. */
NTSTATUS
AvCamUdePrepareIsoUrb(
    _In_ WDFREQUEST request,
    _Inout_ AVCAM_UDE_ISO_STREAM* stream,
    _In_reads_bytes_(frameBytes) const UCHAR* frame,
    _In_ ULONG frameBytes,
    _In_ ULONGLONG firstMicroframe100ns,
    _Out_ PULONG bytesFilled)
{
    return AvCamUdePrepareIsoUrbCore(
        request, stream, frame, frameBytes, NULL,
        firstMicroframe100ns, bytesFilled);
}

/* Staged source-backed URB filling; requires a serialized PASSIVE worker.
 * A frame lease is retained over URB boundaries until a real UVC EOF.
 * Neither function takes WDF request ownership nor completes the URB.
 */
NTSTATUS
AvCamUdePrepareIsoUrbFromPump(
    _In_ WDFREQUEST request,
    _Inout_ AVCAM_UDE_FRAME_PUMP* pump,
    _In_ ULONGLONG firstMicroframe100ns,
    _Out_ PULONG bytesFilled)
{
    return AvCamUdePrepareIsoUrbCore(
        request, NULL, NULL, 0, pump, firstMicroframe100ns, bytesFilled);
}
