# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Avnet
#
# Windows/PowerShell equivalent of provision_rnwf11_cert.py - see that file's
# header comment for what this does and when to run it. No external module
# dependencies (System.IO.Ports is built into PowerShell 5.1+); this does
# implement its own XMODEM+CRC16 sender below since there's no standard
# PowerShell module for it - it follows the same block-numbering/CRC/ACK
# algorithm as the Python `xmodem` package this repo's .py version depends on.
#
# Requires openssl.exe on PATH (e.g. from Git for Windows, or installed separately).

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Port,
    [int]$Baud = 230400,
    [Parameter(Mandatory)] [string]$Duid,
    [Parameter(Mandatory)] [string]$CaCertPath,
    [string]$CaName = "root-ca",
    [string]$CertName = "device-cert",
    [string]$KeyName = "device-key",
    [string]$OutDir = "."
)

$ErrorActionPreference = "Stop"

$FILETYPE_CERT = 1
$FILETYPE_PRIKEY = 2
$XMODEM_CRC16 = 2
$RSA_KEY_BITS = 2048
$CERT_DAYS = 36500  # 100 years, matches the other iotc quickstarts' self-signed certs

function New-DeviceCert {
    param([string]$Duid, [string]$OutDir)

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $keyPath = Join-Path $OutDir "$Duid-pkey.pem"
    $certPath = Join-Path $OutDir "$Duid-cert.pem"
    $subj = "/C=US/ST=IL/L=Chicago/O=IoTConnect/CN=$Duid"

    # RSA, not EC: the RNWF11's plain file-based TLS config path
    # (RNWF_NET_TLS_CONFIG_1) rejects the MQTT connect instantly with an EC
    # (prime256v1) key even though the identical cert/key pair connects fine
    # from a normal TLS client - EC client keys appear to only be supported
    # via the module's separate ATECC608-secure-element code path
    # (RNWF_NET_TLS_ECC608_CONFIG_1), not as a plain uploaded file. RSA is
    # universally supported here.
    & openssl genrsa -out $keyPath $RSA_KEY_BITS
    if ($LASTEXITCODE -ne 0) { throw "openssl genrsa failed" }
    & openssl req -new -days $CERT_DAYS -nodes -x509 -subj $subj -key $keyPath -out $certPath
    if ($LASTEXITCODE -ne 0) { throw "openssl req failed" }

    Write-Host "Generated $certPath and $keyPath"
    Write-Host "`nPaste the certificate below into the IoTConnect console when creating this"
    Write-Host "device (Unique ID `"$Duid`", `"Use my certificate`"):`n"
    Get-Content $certPath | Write-Host

    # Resolve to absolute paths before returning: [System.IO.File]::ReadAllBytes()
    # below resolves relative paths against the .NET process's own working
    # directory, which does NOT always track PowerShell's Set-Location/$PWD -
    # a relative $OutDir here can otherwise silently read from the wrong place.
    return @{ CertPath = (Resolve-Path $certPath).Path; KeyPath = (Resolve-Path $keyPath).Path }
}

function Get-Crc16Xmodem {
    param([byte[]]$Data)
    $crc = 0
    foreach ($b in $Data) {
        $crc = $crc -bxor ([int]$b -shl 8)
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 0x8000) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return $crc
}

function Read-ByteWithTimeout {
    param([System.IO.Ports.SerialPort]$SerialPort)
    try {
        return $SerialPort.ReadByte()
    } catch [System.TimeoutException] {
        return -1
    }
}

function Send-XmodemFile {
    param([System.IO.Ports.SerialPort]$SerialPort, [byte[]]$Data)

    # Wait for the receiver's 'C' handshake byte (CRC mode).
    $SerialPort.ReadTimeout = 10000
    $gotHandshake = $false
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        $b = Read-ByteWithTimeout -SerialPort $SerialPort
        if ($b -eq 0x43) { $gotHandshake = $true; break }
    }
    if (-not $gotHandshake) { throw "Timed out waiting for the XMODEM 'C' (CRC) handshake" }

    $SerialPort.ReadTimeout = 5000
    $blockNum = 1
    $offset = 0
    while ($offset -lt $Data.Length) {
        $chunkLen = [Math]::Min(128, $Data.Length - $offset)
        $chunk = New-Object byte[] 128
        [Array]::Copy($Data, $offset, $chunk, 0, $chunkLen)
        for ($i = $chunkLen; $i -lt 128; $i++) { $chunk[$i] = 0x1A }  # pad with SUB

        $crc = Get-Crc16Xmodem -Data $chunk
        $frame = New-Object byte[] 133  # SOH + blk + ~blk + 128 data + 2 CRC bytes
        $frame[0] = 0x01
        $frame[1] = [byte]($blockNum -band 0xFF)
        $frame[2] = [byte](255 - ($blockNum -band 0xFF))
        [Array]::Copy($chunk, 0, $frame, 3, 128)
        $frame[131] = [byte](($crc -shr 8) -band 0xFF)
        $frame[132] = [byte]($crc -band 0xFF)

        $acked = $false
        for ($retry = 0; $retry -lt 10; $retry++) {
            $SerialPort.Write($frame, 0, $frame.Length)
            $resp = Read-ByteWithTimeout -SerialPort $SerialPort
            if ($resp -eq 0x06) { $acked = $true; break }  # ACK
            # NAK (0x15), timeout (-1), or garbage - just retry the same block
        }
        if (-not $acked) { throw "XMODEM block $blockNum was not acknowledged after 10 retries" }

        $offset += $chunkLen
        $blockNum++
    }

    $eotAcked = $false
    for ($retry = 0; $retry -lt 10; $retry++) {
        $SerialPort.Write([byte[]]@(0x04), 0, 1)  # EOT
        $resp = Read-ByteWithTimeout -SerialPort $SerialPort
        if ($resp -eq 0x06) { $eotAcked = $true; break }
    }
    if (-not $eotAcked) { throw "XMODEM EOT was not acknowledged" }
}

function Send-AtCommand {
    param([System.IO.Ports.SerialPort]$SerialPort, [string]$Command, [int]$TimeoutMs = 5000)

    $SerialPort.ReadTimeout = $TimeoutMs
    $SerialPort.Write("$Command`r`n")
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $response = ""
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        try {
            $response += [char]$SerialPort.ReadByte()
        } catch [System.TimeoutException] {
            continue
        }
        if ($response -match "OK\r\n" -or $response -match "ERROR") { break }
    }
    if ($response -notmatch "OK") {
        throw "AT command failed: '$Command' -> '$response'"
    }
    return $response
}

function Remove-RnwfFile {
    # Best-effort: the RNWF11's filesystem persists across power cycles and
    # AT+FS's LOAD op refuses to overwrite an existing file
    # ("ERROR:7.1,File Exists"), which bites when re-running this script
    # (e.g. provisioning a new device onto a previously-used RNWF11). If the
    # file isn't there yet, the module just replies ERROR, which is fine -
    # we only care that it's gone before we upload.
    param([System.IO.Ports.SerialPort]$SerialPort, [int]$FileType, [string]$FileName)

    $SerialPort.ReadTimeout = 5000
    $SerialPort.Write("AT+FS=3,$FileType,`"$FileName`"`r`n")
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $response = ""
    while ($sw.ElapsedMilliseconds -lt 5000) {
        $b = Read-ByteWithTimeout -SerialPort $SerialPort
        if ($b -eq -1) { continue }
        $response += [char]$b
        if ($response -match "OK\r\n" -or $response -match "ERROR") { break }
    }
}

function Send-FileToRnwf {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$FileType,
        [string]$FileName,
        [byte[]]$Data
    )

    Remove-RnwfFile -SerialPort $SerialPort -FileType $FileType -FileName $FileName
    Write-Host "Uploading $FileName ($($Data.Length) bytes)..."
    $SerialPort.ReadTimeout = 5000
    $SerialPort.Write("AT+FS=1,$FileType,$XMODEM_CRC16,`"$FileName`",$($Data.Length)`r`n")

    # Wait for the '#' raw-mode prompt before starting the XMODEM handshake.
    $deadline = (Get-Date).AddSeconds(5)
    $gotPrompt = $false
    while ((Get-Date) -lt $deadline) {
        $b = Read-ByteWithTimeout -SerialPort $SerialPort
        if ($b -eq [byte][char]'#') { $gotPrompt = $true; break }
    }
    if (-not $gotPrompt) { throw "RNWF11 never entered raw mode for $FileName" }

    Send-XmodemFile -SerialPort $SerialPort -Data $Data

    # The module prints a final "OK" once the transfer completes.
    $SerialPort.ReadTimeout = 10000
    $tail = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt 10000) {
        try {
            $tail += [char]$SerialPort.ReadByte()
        } catch [System.TimeoutException] {
            continue
        }
        if ($tail -match "OK") { break }
    }
    if ($tail -notmatch "OK") { throw "RNWF11 did not confirm $FileName (got: '$tail')" }
    Write-Host "$FileName uploaded OK"
}

if (-not (Test-Path $CaCertPath)) {
    Write-Host "FAILED: CA cert not found: $CaCertPath"
    exit 1
}
# Resolve to an absolute path - see the matching comment in New-DeviceCert.
$CaCertPath = (Resolve-Path $CaCertPath).Path

try {
    $cert = New-DeviceCert -Duid $Duid -OutDir $OutDir

    Write-Host "`nConnecting to RNWF11 at $Port ($Baud baud)..."
    $serialPort = New-Object -TypeName System.IO.Ports.SerialPort -ArgumentList @($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serialPort.Open()
    try {
        Send-AtCommand -SerialPort $serialPort -Command "AT" | Out-Null  # sanity check the module responds

        Send-FileToRnwf -SerialPort $serialPort -FileType $FILETYPE_CERT -FileName $CaName -Data ([System.IO.File]::ReadAllBytes($CaCertPath))
        Send-FileToRnwf -SerialPort $serialPort -FileType $FILETYPE_CERT -FileName $CertName -Data ([System.IO.File]::ReadAllBytes($cert.CertPath))
        Send-FileToRnwf -SerialPort $serialPort -FileType $FILETYPE_PRIKEY -FileName $KeyName -Data ([System.IO.File]::ReadAllBytes($cert.KeyPath))
    } finally {
        $serialPort.Close()
    }
} catch {
    # Covers openssl failing (New-DeviceCert throws), a bad/busy port (Open()
    # throws), and the throws in Send-AtCommand/Send-FileToRnwf/Send-XmodemFile
    # on AT/XMODEM failures - surfaced as one clear failure message.
    Write-Host "`nFAILED: $_"
    exit 1
}

Write-Host "`nSUCCESS: cert, key, and CA cert are all uploaded to the RNWF11."
Write-Host "Move the RNWF11's power jumper back to HOST3V3, mount it on the"
Write-Host "starter kit board (see the README for the correct mikroBUS socket),"
Write-Host "and run provision_device_config.ps1 next."
Write-Host "Pass these filenames to it: -CaName $CaName -CertName $CertName -KeyName $KeyName"
