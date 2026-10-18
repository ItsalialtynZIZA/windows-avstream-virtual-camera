/*
 * UdeCx ISO request preflight (no frame generation or request completion).
 *
 * Correct interpretation of URB_ISOCH_TRANSFER's variable-length packet
 * descriptors is a prerequisite to writing payload bytes. This function
 * does not consume the request: the endpoint callback owns its completion.
 */
#include "AvCamUdeUsb.h"
#include "AvCamUdeIsoUrbLayout.h"

NTSTATUS
AvCamUdeInspectIsoUrb(_In_ WDFREQUEST request)
{
    WDF_REQUEST_PARAMETERS params;
    PURB urb;
    PUCHAR transfer = NULL;
    ULONG mappedBytes = 0;
    ULONG requestedBytes;
    ULONG packets;
    ULONG i;
    NTSTATUS status;

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(request, &params);
    if (params.Type != WdfRequestTypeDeviceControlInternal ||
        params.Parameters.Others.Arg1 == NULL ||
        params.Parameters.Others.IoControlCode !=
            IOCTL_INTERNAL_USB_SUBMIT_URB) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    urb = (PURB)params.Parameters.Others.Arg1;
    if (urb->UrbHeader.Length <
            FIELD_OFFSET(struct _URB_ISOCH_TRANSFER, IsoPacket) ||
        urb->UrbHeader.Function != URB_FUNCTION_ISOCH_TRANSFER) {
        return STATUS_INVALID_PARAMETER;
    }

    packets = urb->UrbIsochronousTransfer.NumberOfPackets;
    requestedBytes = urb->UrbIsochronousTransfer.TransferBufferLength;

    /* Validate the declared variable-length packet array before touching
     * IsoPacket[n]. The buffer is obtained only after structural checks.
     */
    if (AvCamUdeIsoUrbHeaderValid(
            urb->UrbHeader.Length,
            FIELD_OFFSET(struct _URB_ISOCH_TRANSFER, IsoPacket),
            sizeof(USBD_ISO_PACKET_DESCRIPTOR),
            packets,
            requestedBytes,
            requestedBytes,
            (urb->UrbIsochronousTransfer.TransferFlags &
                USBD_TRANSFER_DIRECTION_IN) != 0) !=
        AVCAM_UDE_ISO_URB_VALID) {
        return STATUS_INVALID_PARAMETER;
    }

    status = UdecxUrbRetrieveBuffer(request, &transfer, &mappedBytes);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (transfer == NULL ||
        AvCamUdeIsoUrbHeaderValid(
            urb->UrbHeader.Length,
            FIELD_OFFSET(struct _URB_ISOCH_TRANSFER, IsoPacket),
            sizeof(USBD_ISO_PACKET_DESCRIPTOR),
            packets,
            requestedBytes,
            mappedBytes,
            TRUE) != AVCAM_UDE_ISO_URB_VALID) {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    for (i = 0; i < packets; ++i) {
        const ULONG start = urb->UrbIsochronousTransfer.IsoPacket[i].Offset;
        const ULONG end = (i + 1 < packets) ?
            urb->UrbIsochronousTransfer.IsoPacket[i + 1].Offset :
            requestedBytes;

        if (AvCamUdeIsoUrbOffsetValid(start, end, requestedBytes) !=
            AVCAM_UDE_ISO_URB_VALID) {
            return STATUS_INVALID_PARAMETER;
        }
    }

    /* Preflight only. An async, cancellation-safe handler must take
     * ownership before returning any successfully filled ISO URB.
     * We intentionally do NOT write to the transfer or mutate counters.
     */
    return STATUS_SUCCESS;
}
