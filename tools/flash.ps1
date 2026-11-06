param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('s3-origin', 'renesas', 's3-custom', 'monitor')][string]$Action,
    [string]$ArduinoCli = 'arduino-cli',
    [string]$Sketch,
    [string]$Port,
    [string]$S3Fqbn = 'esp32:esp32:esp32s3:FlashSize=8M,PSRAM=disabled,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=dio',
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
# Git/MinGW shells can pass a module search path from another PowerShell host.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $repoPath
New-Item -ItemType Directory -Force '.build' | Out-Null

function Invoke-Tool([string]$Tool, [string[]]$ToolArgs) {
    & $Tool @ToolArgs
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed (exit $LASTEXITCODE)." }
}

function Find-Ports([string]$Kind) {
    $ports = @(Get-CimInstance Win32_SerialPort)
    if ($Kind -eq 'stock') {
        return @($ports | Where-Object { $_.PNPDeviceID -match 'VID_2341&PID_(1002|006D)' })
    }
    $s3 = @($ports | Where-Object { $_.PNPDeviceID -match 'VID_303A&PID_1001' })
    if ($Kind -eq 'diagnostic') {
        return @($s3 | Where-Object { $_.PNPDeviceID -match '&MI_02' })
    }
    if ($Kind -eq 'native') {
        # ROM CDC/JTAG also has MI_00 on Windows. Only our dual-CDC bridge
        # has a second serial interface at MI_02.
        $diagnostics = @($s3 | Where-Object { $_.PNPDeviceID -match '&MI_02' })
        if ($diagnostics.Count) {
            if ($s3.Count -ne 2) { throw 'Multiple S3 boards found. Connect only the target board.' }
            return @()
        }
        return $s3
    }
    $diagnostics = @($s3 | Where-Object { $_.PNPDeviceID -match '&MI_02' })
    if ($diagnostics.Count) { return $diagnostics }
    return $s3
}

function One-Port([string]$Kind) {
    $ports = @(Find-Ports $Kind)
    if ($ports.Count -gt 1) { throw "Multiple $Kind boards found. Connect only the target Uno R4 WiFi." }
    if ($ports.Count -eq 1) { return $ports[0].DeviceID }
    return $null
}

function Wait-Port([string]$Kind) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $portName = One-Port $Kind
        if ($portName) { return $portName }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "No $Kind USB port appeared. Close monitors; use Download/GND recovery if needed."
}

function Get-RecoveryKit {
    $kit = Join-Path $repoPath '.build\recovery\stock\unor4wifi-update-windows'
    $image = Join-Path $kit 'firmware\UNOR4-WIFI-S3-0.6.1.bin'
    if (-not (Test-Path -LiteralPath $image)) {
        $archive = Join-Path $repoPath '.build\recovery\stock-bridge.zip'
        New-Item -ItemType Directory -Force (Split-Path $archive) | Out-Null
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing 'https://github.com/arduino/uno-r4-wifi-usb-bridge/releases/download/0.6.1/unor4wifi-update-windows.zip' -OutFile $archive
        Expand-Archive -LiteralPath $archive -DestinationPath (Split-Path $kit) -Force
    }
    if ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne 'DD38CF282990CEF5794CB7B7A585D416058DB488DEA6942AF806295FB7FC6148') {
        throw 'Stock S3 firmware checksum mismatch.'
    }
    return $kit
}

function Get-Esptool {
    $local = Join-Path $repoPath '.build\esp-tools\esptool-windows-amd64\esptool.exe'
    if (Test-Path -LiteralPath $local) { return $local }
    $tools = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py'
    $found = @(Get-ChildItem -LiteralPath $tools -Filter esptool.exe -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending)
    if ($found.Count) { return $found[0].FullName }
    throw 'esptool.exe not found. Install esp32:esp32 with Arduino CLI first.'
}

function Enter-S3Download([string]$Kit) {
    $stock = One-Port 'stock'
    $diagnostic = One-Port 'diagnostic'
    $native = One-Port 'native'
    $boardCount = @($stock, $diagnostic, $native | Where-Object { $_ }).Count
    if ($boardCount -gt 1) { throw 'More than one supported board is connected; disconnect the others.' }
    if ($stock) {
        Write-Host "Requesting S3 download mode through stock bridge ($stock)."
        Invoke-Tool (Join-Path $Kit 'bin\unor4wifi-reboot.exe') @()
        return (Wait-Port 'native')
    }
    if ($diagnostic) {
        Write-Host "Requesting S3 download mode through diagnostics ($diagnostic)."
        $serial = [IO.Ports.SerialPort]::new($diagnostic, 115200)
        $serial.DtrEnable = $true
        $serial.RtsEnable = $true
        try {
            $serial.Open()
            try { $serial.BaudRate = 1200 } catch {
                # Windows can report removal during the successful reboot.
                Write-Host 'Waiting for the S3 ROM USB port.'
            }
        } finally { $serial.Dispose() }
        return (Wait-Port 'native')
    }
    if ($native) { return $native } # esptool USB reset handles hardware CDC/JTAG.
    throw 'No supported Uno R4/S3 USB port found. Connect the board and close serial monitors.'
}

if (-not (Get-Command $ArduinoCli -ErrorAction SilentlyContinue)) {
    $fallback = [IO.Path]::GetFullPath((Join-Path $repoPath '..\toolchains\arduino-cli\arduino-cli.exe'))
    if (-not (Test-Path -LiteralPath $fallback)) { throw 'Arduino CLI not found.' }
    $ArduinoCli = $fallback
}

if ($Action -eq 'monitor') {
    if (-not $Port) {
        $ports = @(Find-Ports 'custom') + @(Find-Ports 'stock')
        if ($ports.Count -ne 1) { throw 'Connect one board, or specify make monitor PORT=COM4.' }
        $Port = $ports[0].DeviceID
    }
    if ($Port -notmatch '^COM[0-9]+$') { throw 'Expected a Windows COM port, for example COM4.' }
    Write-Host "Monitoring $Port at 115200 baud. Ctrl+C stops the monitor; close it before flashing."
    if ($BuildOnly) { return }
    Invoke-Tool $ArduinoCli @('monitor', '--port', $Port, '--config', 'baudrate=115200')
    return
}

if ($Action -eq 'renesas') {
    if (-not $Sketch) { $Sketch = 'firmware/bus' }
    $build = Join-Path $repoPath '.build\swap-renesas'
    Invoke-Tool $ArduinoCli @('compile', '--fqbn', 'arduino:renesas_uno:unor4wifi', '--library', (Join-Path $repoPath 'firmware\shared'), '--build-path', $build, $Sketch)
    if ($BuildOnly) { return }
    $portName = One-Port 'stock'
    if (-not $portName) { throw 'Stock S3 bridge is required. Run make s3-origin first.' }
    Invoke-Tool $ArduinoCli @('upload', '--fqbn', 'arduino:renesas_uno:unor4wifi', '--port', $portName, '--input-dir', $build)
    Write-Output "Renesas uploaded through $portName."
    return
}

$kit = Get-RecoveryKit
$esptool = Get-Esptool
if ($Action -eq 's3-custom') {
    if (-not $Sketch) { $Sketch = 'firmware/s3' }
    $build = Join-Path $repoPath '.build\swap-s3'
    Invoke-Tool $ArduinoCli @('compile', '--fqbn', $S3Fqbn, '--library', (Join-Path $repoPath 'firmware\shared'), '--build-path', $build, $Sketch)
    $name = Split-Path ([IO.Path]::GetFullPath($Sketch).TrimEnd('\', '/')) -Leaf
    $image = Join-Path $build "$name.ino.merged.bin"
    if (-not (Test-Path -LiteralPath $image)) { throw "Missing merged firmware: $image" }
} else {
    $image = Join-Path $kit 'firmware\UNOR4-WIFI-S3-0.6.1.bin'
}
if ($BuildOnly) { Write-Output "Prepared $image; no flash written."; return }
$downloadPort = Enter-S3Download $kit
Invoke-Tool $esptool @('--chip', 'esp32s3', '--port', $downloadPort, '--baud', '115200',
    '--before', 'usb-reset', '--after', 'hard-reset', '--no-stub', 'write-flash', '--compress',
    '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '8MB', '0x0', $image)
if ($Action -eq 's3-origin') { $portName = Wait-Port 'stock' } else { $portName = Wait-Port 'custom' }
Write-Output "$Action complete; USB port: $portName"
