#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#include <ude/1.0/UdeCx.h>
#include "AvCamUdeControl.h"
#include "AvCamUdeEndpointDrain.h"
#include "AvCamUdeAsyncIso.h"
#include "AvCamUdeIso.h"
#include "AvCamUdeIsoClock.h"
#include "AvCamUdeFramePump.h"
#include "AvCamUdeUploadProtocol.h"

/* One emulated high-speed UVC device on one software USB controller.
 * The current milestone is compile-only. No child is plugged in unless
 * the controller's explicit experimental gate enables the code path.
 */
typedef struct _AVCAM_UDE_CONTROLLER_CONTEXT {
    UDECXUSBDEVICE UsbDevice;
    UDECXUSBENDPOINT ControlEndpoint;
    UDECXUSBENDPOINT VideoEndpoint;
    BOOLEAN ChildPlugged;
    /* PASSIVE_LEVEL lock shared by MJPEG publication and future ISO pump.
     * Slots live until controller cleanup, never until one URB completion.
     * FrameStorage allocation is lazy and happens only after child plug-in.
     */
    WDFWAITLOCK FrameLock;
    PUCHAR FrameStorage[2];
    AVCAM_UDE_FRAME_SOURCE FrameSource;
    BOOLEAN FrameSourceReady;
    ULONGLONG UploadedFrames;
} AVCAM_UDE_CONTROLLER_CONTEXT, *PAVCAM_UDE_CONTROLLER_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AVCAM_UDE_CONTROLLER_CONTEXT, AvCamUdeControllerGetContext);

typedef struct _AVCAM_UDE_USB_DEVICE_CONTEXT {
    WDFDEVICE Controller;
    AVCAM_UVC_STREAM_STATE Stream;
    /* Updated by successful UVC VS COMMIT under controller FrameLock. */
    ULONGLONG CommitEpoch;
} AVCAM_UDE_USB_DEVICE_CONTEXT, *PAVCAM_UDE_USB_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AVCAM_UDE_USB_DEVICE_CONTEXT, AvCamUdeUsbGetContext);

/* An endpoint queue belongs to the controller; retain the identity
 * of the UdeCx child and which pipe it represents for safe dispatch. */
typedef struct _AVCAM_UDE_ENDPOINT_QUEUE_CONTEXT {
    UDECXUSBDEVICE UsbDevice;
    UDECXUSBENDPOINT Endpoint;
    UCHAR EndpointAddress;
} AVCAM_UDE_ENDPOINT_QUEUE_CONTEXT, *PAVCAM_UDE_ENDPOINT_QUEUE_CONTEXT;

/* Created on the native UdeCx endpoint object, not the controller. The
 * spinlock guards the pure state machine. WorkItem acknowledges PURGE at
 * PASSIVE_LEVEL after the final request has left.
 */
typedef struct _AVCAM_UDE_ENDPOINT_CONTEXT {
    WDFDEVICE Controller;
    WDFSPINLOCK Lock;
    WDFWORKITEM PurgeWorkItem;
    AVCAM_UDE_ENDPOINT_DRAIN Drain;
    /* One in-flight request in UdeCx's sequential ISO endpoint queue. */
    WDFDPC IsoCompleteDpc;
    WDFREQUEST PendingIsoRequest;
    BOOLEAN IsoDpcRunning;
    AVCAM_UDE_FRAME_PUMP Pump;
    BOOLEAN PumpInitialized;
    BOOLEAN PumpResetPending;
    ULONGLONG SeenCommitEpoch;
    AVCAM_UDE_ISO_CLOCK Clock;
} AVCAM_UDE_ENDPOINT_CONTEXT, *PAVCAM_UDE_ENDPOINT_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AVCAM_UDE_ENDPOINT_CONTEXT, AvCamUdeEndpointGetContext);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AVCAM_UDE_ENDPOINT_QUEUE_CONTEXT, AvCamUdeEndpointQueueGetContext);

NTSTATUS AvCamUdeInspectIsoUrb(_In_ WDFREQUEST Request);
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS AvCamUdeStageIsoRequestPassive(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request);
EVT_WDF_DPC AvCamUdeEvtIsoCompleteDpc;
/* PASSIVE_LEVEL only. Prepares payload/packet descriptors, but never completes
 * or takes ownership of the WDF request. Caller owns cancellation and state. */
NTSTATUS AvCamUdePrepareIsoUrb(
    _In_ WDFREQUEST Request,
    _Inout_ AVCAM_UDE_ISO_STREAM* Stream,
    _In_reads_bytes_(FrameBytes) const UCHAR* Frame,
    _In_ ULONG FrameBytes,
    _In_ ULONGLONG FirstMicroframe100ns,
    _Out_ PULONG BytesFilled);

/* Prepared MJPEG source-backed ISO URB. Call at PASSIVE_LEVEL while holding
 * exclusive stream ownership, and reset the pump when a request is aborted.
 * Does not complete, queue, cancel or acquire any WDF request.
 */
NTSTATUS AvCamUdePrepareIsoUrbFromPump(
    _In_ WDFREQUEST Request,
    _Inout_ AVCAM_UDE_FRAME_PUMP* Pump,
    _In_ ULONGLONG FirstMicroframe100ns,
    _Out_ PULONG BytesFilled);

/* Preflight final ISO packet metadata and set UdeCx transferred byte count.
 * Does NOT own/complete the WDFREQUEST; needs PASSIVE_LEVEL and a stable
 * exclusively-owned URB after successful ISO staging.
 */
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS AvCamUdeSealPreparedIsoUrbPassive(
    _In_ WDFREQUEST Request,
    _In_ ULONG ReportedBytes);

NTSTATUS AvCamUdeUsbCreateAndPlug(_In_ WDFDEVICE Controller);
VOID AvCamUdeUsbDisconnect(_In_ WDFDEVICE Controller);

EVT_UDECX_USB_DEVICE_DEFAULT_ENDPOINT_ADD AvCamUdeEvtDefaultEndpointAdd;
EVT_UDECX_USB_DEVICE_ENDPOINT_ADD AvCamUdeEvtDynamicEndpointAdd;
EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE AvCamUdeEvtEndpointsConfigure;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL AvCamUdeEvtEndpointIo;
EVT_UDECX_USB_ENDPOINT_RESET AvCamUdeEvtEndpointReset;
EVT_UDECX_USB_ENDPOINT_START AvCamUdeEvtEndpointStart;
EVT_UDECX_USB_ENDPOINT_PURGE AvCamUdeEvtEndpointPurge;
EVT_WDF_WORKITEM AvCamUdeEvtEndpointPurgeWorkItem;
