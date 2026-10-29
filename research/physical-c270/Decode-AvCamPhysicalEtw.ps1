# Read an existing AvCam Device MFT ETL without changing camera configuration.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EtlPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$provider = [guid]'{A56F32CA-31E5-4C6D-95F2-74DB9DA141E9}'
if (-not (Test-Path -LiteralPath $EtlPath -PathType Leaf)) {
    throw "ETL file not found: $EtlPath"
}
$events = @(Get-WinEvent -Path $EtlPath -Oldest -ErrorAction Stop |
    Where-Object { $_.ProviderId -eq $provider })
Write-Host ('AVCAM_ETL_EVENT_COUNT=' + $events.Count)
$messages = @()
foreach ($event in $events) {
    $parts = [System.Collections.Generic.List[string]]::new()
    try {
        $xml = [xml]$event.ToXml()
        foreach ($node in $xml.SelectNodes('//*[local-name()="EventData"]/*[local-name()="Data"]')) {
            if (-not [string]::IsNullOrWhiteSpace($node.InnerText)) {
                $parts.Add($node.InnerText)
            }
        }
        foreach ($node in $xml.SelectNodes('//*[local-name()="UserData"]//*[not(*)]')) {
            if (-not [string]::IsNullOrWhiteSpace($node.InnerText)) {
                $parts.Add($node.InnerText)
            }
        }
    } catch {
        Write-Warning ('XML decode failed for event ' + $event.RecordId)
    }
    # EventWriteString from a manifestless ETW provider may be emitted by
    # Get-WinEvent as ProcessingErrorData/ErrorCode=15003, with raw UTF-16LE
    # bytes in EventPayload rather than decoded EventData properties.
    # Only decode bounded diagnostic text, never video or user media.
    if ($parts.Count -eq 0 -and $null -ne $xml) {
        $payloadNodes = $xml.SelectNodes('//*[local-name()="ProcessingErrorData"]/*[local-name()="EventPayload"]')
        foreach ($node in $payloadNodes) {
            $hex = ([string]$node.InnerText).Trim()
            if ($hex.Length -eq 0 -or $hex.Length -gt 131072 -or
                $hex.Length % 4 -ne 0 -or $hex -cnotmatch '^[0-9A-Fa-f]+
    if ($parts.Count -eq 0 -and -not [string]::IsNullOrWhiteSpace($event.Message)) {
        $parts.Add($event.Message)
    }
    $message = ($parts -join ' | ')
    $messages += $message
    Write-Host ('ETW {0} ID={1} PID={2} MESSAGE={3}' -f
        $event.TimeCreated.ToString('HH:mm:ss.fff'), $event.Id, $event.ProcessId, $message)
    if ($parts.Count -eq 0) {
        Write-Host ('EVENT_XML=' + $event.ToXml())
    }
}
Write-Host ('MFT_INITIALIZE_OBSERVED=' + (@($messages | Where-Object { $_ -match 'InitializeTransform called' }).Count -gt 0))
Write-Host ('MFT_QUEUE_FORMAT_EVENTS=' + @($messages | Where-Object { $_ -match 'QueueFormat' }).Count)
Write-Host ('MFT_SAMPLE_REPLACED_EVENTS=' + @($messages | Where-Object { $_ -match 'SampleReplaced' }).Count)
Write-Host 'AVCAM_ETL_READ_ONLY_COMPLETE'
) {
                continue
            }
            try {
                $raw = New-Object byte[] ($hex.Length / 2)
                for ($i = 0; $i -lt $raw.Length; ++$i) {
                    $raw[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
                }
                $decoded = [Text.Encoding]::Unicode.GetString($raw).TrimEnd([char]0)
                if (-not [string]::IsNullOrWhiteSpace($decoded)) {
                    $parts.Add($decoded)
                }
            } catch {
                Write-Warning ('Raw diagnostic ETW payload decoding failed for event ' + $event.RecordId)
            }
        }
    }
    if ($parts.Count -eq 0) {
        foreach ($entry in $event.Properties) {
            if ($null -ne $entry.Value) { $parts.Add([string]$entry.Value) }
        }
    }
    if ($parts.Count -eq 0 -and -not [string]::IsNullOrWhiteSpace($event.Message)) {
        $parts.Add($event.Message)
    }
    $message = ($parts -join ' | ')
    $messages += $message
    Write-Host ('ETW {0} ID={1} PID={2} MESSAGE={3}' -f
        $event.TimeCreated.ToString('HH:mm:ss.fff'), $event.Id, $event.ProcessId, $message)
    if ($parts.Count -eq 0) {
        Write-Host ('EVENT_XML=' + $event.ToXml())
    }
}
Write-Host ('MFT_INITIALIZE_OBSERVED=' + (@($messages | Where-Object { $_ -match 'InitializeTransform called' }).Count -gt 0))
Write-Host ('MFT_QUEUE_FORMAT_EVENTS=' + @($messages | Where-Object { $_ -match 'QueueFormat' }).Count)
Write-Host ('MFT_SAMPLE_REPLACED_EVENTS=' + @($messages | Where-Object { $_ -match 'SampleReplaced' }).Count)
Write-Host 'AVCAM_ETL_READ_ONLY_COMPLETE'
