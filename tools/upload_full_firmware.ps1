[CmdletBinding()]
param(
    [string]$Port
)

$ErrorActionPreference = 'Stop'
trap {
    Write-Host ''
    Write-Host ("ERROR: {0}" -f $_.Exception.Message) -ForegroundColor Red
    exit 1
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$image = Join-Path $root 'Releases\ShelfClock-Full-Firmware.bin'
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$esptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'

foreach ($required in @($image, $python, $esptool)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file was not found: $required"
    }
}

Write-Host ''
Write-Host 'Windows port-device records (Unknown may mean disconnected):'
$pnpDevices = @()
try {
    $pnpDevices = @(Get-PnpDevice -Class Ports -ErrorAction Stop)
    $pnpDevices | Select-Object FriendlyName, Status | Format-Table -AutoSize | Out-Host
} catch {
    Write-Warning ("Get-PnpDevice could not list ports: {0}" -f $_.Exception.Message)
}

try {
    $activePortNames = @([System.IO.Ports.SerialPort]::GetPortNames() |
        ForEach-Object { $_.ToUpperInvariant() })
} catch {
    $activePortNames = @()
}

$choices = foreach ($portName in $activePortNames) {
    $device = $pnpDevices | Where-Object { $_.FriendlyName -match "\($([regex]::Escape($portName))\)" } |
        Select-Object -First 1
    [PSCustomObject]@{
        Port = $portName
        Name = if ($null -eq $device) { $portName } else { $device.FriendlyName }
    }
}
$choices = @($choices | Sort-Object { [int]($_.Port -replace '^COM', '') })
if ($choices.Count -eq 0) {
    throw ('Windows currently has no active COM ports. The Unknown entries above are ' +
        'remembered devices, not connected ports. Reconnect the clock by USB and confirm ' +
        'that its port appears in [System.IO.Ports.SerialPort]::GetPortNames() before retrying.')
}

if (-not [string]::IsNullOrWhiteSpace($Port)) {
    $selectedPort = $Port.Trim().ToUpperInvariant()
    if ($selectedPort -notmatch '^COM\d+$') {
        throw "Invalid port '$Port'. Use a COM name such as COM8."
    }
    if ($selectedPort -notin $activePortNames) {
        throw "$selectedPort is not currently active. Reconnect the clock or correct its USB/serial driver before retrying."
    }
} else {
    Write-Host 'Connected ports available to esptool:'
    for ($index = 0; $index -lt $choices.Count; $index++) {
        Write-Host ("  [{0}] {1} - {2}" -f ($index + 1), $choices[$index].Port, $choices[$index].Name)
    }
    $selection = Read-Host ("Enter the bracketed selection number [1-{0}]" -f $choices.Count)
    if ($selection -notmatch '^\d+$' -or [int]$selection -lt 1 -or [int]$selection -gt $choices.Count) {
        throw "Invalid selection '$selection'. Enter only a bracketed menu number from 1 through $($choices.Count)."
    }
    $selectedPort = $choices[[int]$selection - 1].Port
}

Write-Host ''
Write-Warning 'This complete installation replaces firmware, filesystem, settings, schedules, songs, and Wi-Fi data.'
Write-Host "Image: $image"
Write-Host "Port:  $selectedPort"
$confirmation = Read-Host 'Type ERASE to continue'
if ($confirmation -cne 'ERASE') {
    Write-Host 'Upload cancelled.'
    exit 0
}

& $python $esptool --chip esp32 --port $selectedPort write_flash 0x0 $image
if ($LASTEXITCODE -ne 0) {
    throw "Full firmware upload failed on $selectedPort."
}

Write-Host ''
Write-Host 'ShelfClock Full Firmware was uploaded successfully.'
