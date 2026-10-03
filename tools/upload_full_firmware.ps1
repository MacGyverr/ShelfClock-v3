[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
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
Write-Host 'Detected serial ports:'
$pnpPorts = @()
try {
    $pnpPorts = @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction Stop |
        Where-Object { $_.FriendlyName -match '\(COM\d+\)' } |
        Sort-Object FriendlyName)
    $pnpPorts | Select-Object FriendlyName, Status | Format-Table -AutoSize | Out-Host
} catch {
    Write-Warning 'Get-PnpDevice was unavailable; using the serial-port list instead.'
}

$choices = @()
foreach ($device in $pnpPorts) {
    if ($device.FriendlyName -match '\((COM\d+)\)') {
        $choices += [PSCustomObject]@{
            Port = $Matches[1]
            Name = $device.FriendlyName
        }
    }
}
if ($choices.Count -eq 0) {
    $choices = @([System.IO.Ports.SerialPort]::GetPortNames() |
        Sort-Object |
        ForEach-Object { [PSCustomObject]@{ Port = $_; Name = $_ } })
}
if ($choices.Count -eq 0) {
    throw 'No COM ports were detected. Connect the clock by USB and try again.'
}

Write-Host 'Choose the ShelfClock port:'
for ($index = 0; $index -lt $choices.Count; $index++) {
    Write-Host ("  [{0}] {1}" -f ($index + 1), $choices[$index].Name)
}
$selection = Read-Host 'Port number or COM name'
if ($selection -match '^\d+$' -and [int]$selection -ge 1 -and [int]$selection -le $choices.Count) {
    $port = $choices[[int]$selection - 1].Port
} elseif ($selection -match '^COM\d+$') {
    $port = $selection.ToUpperInvariant()
} else {
    throw "Invalid port selection: $selection"
}

Write-Host ''
Write-Warning 'This complete installation replaces firmware, filesystem, settings, schedules, songs, and Wi-Fi data.'
Write-Host "Image: $image"
Write-Host "Port:  $port"
$confirmation = Read-Host 'Type ERASE to continue'
if ($confirmation -cne 'ERASE') {
    Write-Host 'Upload cancelled.'
    exit 0
}

& $python $esptool --chip esp32 --port $port write_flash 0x0 $image
if ($LASTEXITCODE -ne 0) {
    throw "Full firmware upload failed on $port."
}

Write-Host ''
Write-Host 'ShelfClock Full Firmware was uploaded successfully.'
