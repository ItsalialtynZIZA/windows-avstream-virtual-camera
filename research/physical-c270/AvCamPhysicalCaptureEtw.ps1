# AvCam Physical C270 MFT ETW capture. Captures diagnostic text only.
# Run elevated with physical Logitech C270 connected. Close Windows Camera
# BEFORE starting this script, then open it while tracing is active.
[CmdletBinding()]
param(
    [ValidateRange(10,120)][int]$Seconds = 30,
    [ValidateSet('WindowsCamera','DeepLiveCam','ChromeWebRTC')][string]$Target = 'WindowsCamera'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$p = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as administrator.'
}
$provider = '{A56F32CA-31E5-4C6D-95F2-74DB9DA141E9}'
$session = 'AvCamPhysicalMftTrace-' + $PID
$folder = Join-Path $env:TEMP ('AvCamPhysicalETW-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$etl = Join-Path $folder 'AvCamPhysicalMft.etl'
$logman = Join-Path $env:WINDIR 'System32\logman.exe'
$started = $false
Write-Host "Capturing AvCam MFT ETW for $Seconds seconds; target=$Target."
try {
    & $logman start $session -p $provider 0xFFFFFFFF 5 -o $etl -ets | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "logman could not start session, code $LASTEXITCODE" }
    $started = $true
    if ($Target -eq 'DeepLiveCam') {
        Write-Host 'TRACE_STARTED: Launch Deep Live Cam NOW; attempt to start the physical Logitech C270 once. Note the exact error.'
        Write-Host 'Do not open Chrome or Windows Camera during the reproduction.'
    } elseif ($Target -eq 'ChromeWebRTC') {
        Write-Host 'TRACE_STARTED: In an already open Chrome WebRTC resolution test, select ORIGINAL Logi C270,'
        Write-Host 'then try VGA (640x480) and HD/720p (1280x720), one at a time.'
        Write-Host 'Keep OBS and AvCamPhysicalBridge running if you are testing their pipeline.'
        Write-Host 'This trace reads AvCam MFT event messages; no video images, camera registry, or driver settings are modified.'
    } else {
        Write-Host 'TRACE_STARTED: Open Windows Camera NOW; keep physical C270 preview active.'
    }
    Start-Sleep -Seconds $Seconds
} finally {
    if ($started) { & $logman stop $session -ets 2>&1 | Out-Host }
}
Write-Host "ETL_FILE=$etl"
if (Test-Path -LiteralPath $etl) {
    try {
        # An ETL file with no events is a valid diagnostic outcome.
        # Get-WinEvent can throw NoMatchingEventsFound in that case; it
        # must not be misreported as a decoder failure.
        $allEvents = @()
        try {
            $allEvents = @(Get-WinEvent -Path $etl -Oldest -ErrorAction Stop)
        } catch {
            if ($_.FullyQualifiedErrorId -notmatch 'NoMatchingEventsFound') { throw }
        }
        $records = @($allEvents | Where-Object { $_.ProviderId -eq [guid]$provider })
        Write-Host ('ETW_PROVIDER_RECORDS=' + $records.Count)
        # EventWriteString publishes text via event properties. Some Windows
        # providers expose it only in Message, so support both representations.
        function Get-AvCamEtwText($record) {
            $values = @($record.Properties | ForEach-Object { [string]$_.Value })
            if ($values.Count -gt 0 -and ($values -join '').Trim()) {
                return ($values -join ' ')
            }
            if ($record.Message) { return [string]$record.Message }
            return ''
        }
        $messages = @($records | ForEach-Object { Get-AvCamEtwText $_ })
        for ($i = 0; $i -lt [Math]::Min(80, $records.Count); ++$i) {
            Write-Host ('ETW {0} {1}' -f $records[$i].TimeCreated.ToString('HH:mm:ss'),$messages[$i])
        }
        if ($records.Count -eq 0) {
            Write-Warning 'No AvCam MFT events observed. This does not prove that Device MFT was never activated: the host may use a different capture pipeline or tracing may have failed.'
            Write-Host 'DIAGNOSIS=NO_MFT_EVENTS_OBSERVED'
            Write-Host 'Keep the ETL file for further analysis.'
        } else {
            $initialized = @($messages | Where-Object { $_ -match 'AvCamDeviceMftInitialize|InitializeTransform called' }).Count -gt 0
            $formats = @($messages | Where-Object { $_ -match 'AvCamQueueNegotiated|QueueFormat' }).Count
            $replaced = @($messages | Where-Object { $_ -match 'SampleReplaced' }).Count
            $passthrough = @($messages | Where-Object { $_ -match 'SampleSkipped|AvCamMultiAppSafePassThrough' }).Count
            Write-Host ('AVCAM_MFT_INITIALIZED_OBSERVED=' + $initialized)
            Write-Host ('AVCAM_MFT_QUEUE_EVENTS_OBSERVED=' + $formats)
            Write-Host ('AVCAM_MFT_REPLACEMENT_EVENTS_OBSERVED=' + $replaced)
            Write-Host ('AVCAM_MFT_PASSTHROUGH_EVENTS_OBSERVED=' + $passthrough)
            # This event exists only after successful InitializeTransform of
            # the experimental AvCam MFT. It reports the CONNECTED transform
            # counts, not the identity or pin contract of the OEM Sonix DLL.
            $topologyEvents = @($messages | Where-Object { $_ -match 'AvCamDeviceMftPinTopology' })
            Write-Host ('AVCAM_DMFT_TOPOLOGY_EVENTS_OBSERVED=' + $topologyEvents.Count)
            $validTopology = 0
            $mismatched = 0
            foreach ($event in $topologyEvents) {
                $m = [regex]::Match($event, 'upstream_output=(\d+) avcam_input=(\d+) avcam_output=(\d+) identity=unknown')
                if (-not $m.Success) { continue }
                $validTopology++
                $up = [int64]$m.Groups[1].Value
                $inside = [int64]$m.Groups[2].Value
                if ($up -ne $inside) { $mismatched++ }
                Write-Host ('AVCAM_DMFT_CONNECTED_UPSTREAM_OUTPUT=' + $up)
                Write-Host ('AVCAM_DMFT_LOCAL_INPUT_COUNT=' + $inside)
                Write-Host ('AVCAM_DMFT_LOCAL_OUTPUT_COUNT=' + [int64]$m.Groups[3].Value)
            }
            Write-Host ('AVCAM_DMFT_TOPOLOGY_PARSED=' + $validTopology)
            Write-Host ('AVCAM_DMFT_UPSTREAM_INPUT_COUNT_MISMATCH_EVENTS=' + $mismatched)
            Write-Host 'AVCAM_DMFT_CONNECTED_UPSTREAM_IDENTITY=UNKNOWN'
            Write-Host 'AVCAM_SONIX_PIN_INTEROP=NOT_VERIFIED'
            Write-Host 'AVCAM_SONIX_CHAIN_INSTALL=BLOCKED'
            Write-Host 'NOTE: ETW summaries count sampled diagnostic EVENTS, not delivered video frames.'
            Write-Host 'EVIDENCE_LEVEL=DEVICE_MFT_ONLY'
            Write-Host 'No event above proves an individual Chrome or Deep Live Cam consumer.'
            Write-Host 'Note: observed ETW events are not a complete frame count.'
        }
    } catch {
        Write-Warning ('Windows could not decode ETW payloads: ' + $_.Exception.Message)
        Write-Host 'The ETL was still saved for inspection.'
    }
}
Write-Host ('ETW_TARGET=' + $Target)
Write-Host 'ETW_TRACE_FINISHED_CAMERA_UNCHANGED'
