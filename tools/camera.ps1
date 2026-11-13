param(
    [string]$Port,
    [string]$ArduinoCli = 'arduino-cli',
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $repoPath
$fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=default,FlashMode=dio,PartitionScheme=default_8MB'
if (-not (Test-Path -LiteralPath 'firmware/camera/config.h')) {
    throw 'Copy firmware/camera/config.example.h to config.h and fill Wi-Fi and UART pins (ignored by Git).'
}
if (-not $BuildOnly) {
    if ($Port -notmatch '^COM[0-9]+$') { throw 'Specify the camera TTL USB port explicitly: make camera PORT=COMx.' }
    # Some CH340 drivers expose a working COM port only through PnPEntity,
    # not Win32_SerialPort. Check its actual PnP identity before uploading.
    $device = @(Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.Name -match ('\(' + [regex]::Escape($Port) + '\)$') -and $_.ConfigManagerErrorCode -eq 0
    })
    if ($device.Count -ne 1 -or $device[0].PNPDeviceID -notmatch 'VID_1A86') {
        throw 'Expected the camera CH340 TTL USB port (VID_1A86). This tool will not flash the Uno.'
    }
}
New-Item -ItemType Directory -Force '.build' | Out-Null
& $ArduinoCli compile --fqbn $fqbn --library (Join-Path $repoPath 'firmware/shared') --build-path (Join-Path $repoPath '.build/camera') 'firmware/camera'
if ($LASTEXITCODE -ne 0) { throw "Camera build failed (exit $LASTEXITCODE)." }
if ($BuildOnly) { Write-Output 'Camera firmware built; no flash written.'; return }
& $ArduinoCli upload --fqbn $fqbn --port $Port --input-dir (Join-Path $repoPath '.build/camera')
if ($LASTEXITCODE -ne 0) { throw "Camera upload failed (exit $LASTEXITCODE)." }
Write-Output "Camera uploaded through $Port; monitor at 115200 baud. Uno unchanged."
