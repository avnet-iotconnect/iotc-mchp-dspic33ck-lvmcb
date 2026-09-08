# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Avnet
#
# Windows/PowerShell equivalent of provision_device_config.py - see that file's
# header comment for what this does and when to run it. This script has no
# external module dependencies (System.IO.Ports and Invoke-RestMethod are
# both built into PowerShell 5.1+), so there's no install step before running it.

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Port,
    [int]$Baud = 115200,
    [Parameter(Mandatory)] [string]$WifiSsid,
    [Parameter(Mandatory)] [string]$WifiPassword,
    [Parameter(Mandatory)] [string]$Cpid,
    [Parameter(Mandatory)] [string]$Env,
    [Parameter(Mandatory)] [string]$Duid,
    [ValidateSet("aws", "az")] [string]$Platform = "aws",
    [string]$CaName = "root-ca",
    [string]$CertName = "device-cert",
    [string]$KeyName = "device-key"
)

$ErrorActionPreference = "Stop"
$MqttPort = 8883

function Invoke-DraRequest {
    # Invoke-RestMethod throws on non-2xx HTTP responses (same as Python's
    # urllib.request.urlopen), which the DRA API uses for some error cases
    # (e.g. an unrecognized CPID/Environment) - without this, that would
    # surface as a raw HttpResponseException stack trace instead of the
    # API's actual JSON error message.
    param([string]$Uri, [string]$What)
    try {
        return Invoke-RestMethod -Uri $Uri -Method Get
    } catch {
        $body = $_.ErrorDetails.Message
        $parsed = $null
        if ($body) {
            try { $parsed = $body | ConvertFrom-Json } catch { }
        }
        if ($parsed) {
            throw "$What failed. status=$($parsed.status) message=$($parsed.message)"
        }
        if ($body) {
            throw "$What failed: $body"
        }
        throw "$What failed: $_"
    }
}

function Resolve-ConnectionInfo {
    param([string]$Cpid, [string]$Env, [string]$Duid, [string]$Platform)

    $discoveryUrl = "https://discovery.iotconnect.io/api/v2.1/dsdk/cpId/$([uri]::EscapeDataString($Cpid))/env/$([uri]::EscapeDataString($Env))?pf=$Platform"
    Write-Host "Requesting Discovery Data $discoveryUrl..."
    $disc = Invoke-DraRequest -Uri $discoveryUrl -What "Discovery"
    if ($disc.status -ne 200 -or $disc.d.ec -ne 0) {
        throw "Discovery failed. status=$($disc.status) ec=$($disc.d.ec) message=$($disc.message)"
    }

    $identityUrl = "$($disc.d.bu)/uid/$([uri]::EscapeDataString($Duid))"
    Write-Host "Requesting Identity Data $identityUrl..."
    $ident = Invoke-DraRequest -Uri $identityUrl -What "Identity"
    if ($ident.status -ne 200 -or $ident.d.ec -ne 0) {
        throw "Identity failed. status=$($ident.status) ec=$($ident.d.ec) message=$($ident.message)"
    }
    if ([string]::IsNullOrEmpty($ident.d.p.topics.rpt)) {
        throw "Identity response did not include a telemetry (rpt) topic"
    }

    return $ident.d.p
}

try {
    $identity = Resolve-ConnectionInfo -Cpid $Cpid -Env $Env -Duid $Duid -Platform $Platform
} catch {
    Write-Host "FAILED: could not resolve device connection info: $_"
    exit 1
}

Write-Host "Resolved broker host: $($identity.h)"
Write-Host "Resolved MQTT client ID: $($identity.id)"
Write-Host "Resolved MQTT username: $(if ($identity.un) { $identity.un } else { '(none)' })"
Write-Host "Resolved telemetry topic: $($identity.topics.rpt)"

$fields = [ordered]@{
    WIFI_SSID        = $WifiSsid
    WIFI_PASSWORD    = $WifiPassword
    IOTC_CPID        = $Cpid
    IOTC_ENV         = $Env
    # The firmware only ever uses this field as the MQTT client ID, so use
    # IoTConnect's own resolved client ID here rather than the raw -Duid:
    # they're the same for "dedicated" instances, but for "shared" instances
    # (e.g. POC/trial accounts) IoTConnect assigns a different client ID
    # (often "CPID-DUID"), and sending the wrong one gets the connection
    # silently rejected by the broker.
    IOTC_DUID        = $identity.id
    MQTT_BROKER_HOST = $identity.h
    MQTT_BROKER_PORT = $MqttPort
    MQTT_USERNAME    = $(if ($identity.un) { $identity.un } else { "" })
    MQTT_PUB_TOPIC   = $identity.topics.rpt
    RNWF_CA_NAME     = $CaName
    RNWF_CERT_NAME   = $CertName
    RNWF_KEY_NAME    = $KeyName
}

Write-Host "`nConnecting to $Port at $Baud baud..."
try {
    $serialPort = New-Object -TypeName System.IO.Ports.SerialPort -ArgumentList @($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serialPort.NewLine = "`n"
    $serialPort.ReadTimeout = 5000
    $serialPort.Open()
} catch {
    Write-Host "FAILED: could not open $Port`: $_"
    exit 1
}

try {
    Start-Sleep -Milliseconds 200  # let the port settle before writing

    # The firmware's WiFi/MQTT connect attempts each block for many seconds
    # at a time (and this is a polled, non-interrupt-driven UART, so bytes
    # sent while it's not actively reading are lost, not just delayed) - a
    # single one-shot send has a real chance of landing in one of those
    # windows and getting no response at all, even though the device is
    # working fine. Resend the whole handshake periodically until either it
    # succeeds or we've comfortably outlasted a full connect-retry cycle.
    $overallTimeoutSec = 90
    $retryIntervalSec = 5
    $deadline = (Get-Date).AddSeconds($overallTimeoutSec)
    $attempt = 0
    $provisioned = $false

    while (-not $provisioned) {
        $attempt++
        $serialPort.DiscardInBuffer()  # discard anything stale from a previous attempt

        $serialPort.Write("PROVISION`n")
        foreach ($key in $fields.Keys) {
            $serialPort.Write("$key=$($fields[$key])`n")
        }
        $serialPort.Write("END`n")

        $response = $null
        try {
            $response = $serialPort.ReadLine().Trim()
        } catch [System.TimeoutException] {
            $response = $null
        }

        if ($response -eq "OK") {
            Write-Host "Device confirmed: OK"
            $provisioned = $true
        } elseif ($response) {
            Write-Host "FAILED: device reported an error: $response"
            exit 1
        } elseif ((Get-Date) -ge $deadline) {
            Write-Host "FAILED: device never responded - check the port/baud rate, and that the firmware is running"
            exit 1
        } else {
            Write-Host "No response yet (attempt $attempt) - the board may be mid-connection-attempt and not listening right now; retrying..."
            Start-Sleep -Seconds $retryIntervalSec
        }
    }
} finally {
    $serialPort.Close()
}

Write-Host "SUCCESS: device provisioned. It should now connect to WiFi and IoTConnect."
