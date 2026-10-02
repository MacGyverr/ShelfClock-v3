[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('espwroom32', 'espwroom32_test', 'espwroom32_isolation')]
    [string]$Environment,
    [Parameter(Mandatory = $true)]
    [ValidateSet('Standalone', 'ESPHome')]
    [string]$Target
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$platformio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
$platformioPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$platformioEsptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'
$esphomePython = Join-Path $root '.tools\esphome\Scripts\python.exe'
$mkLittleFs = Join-Path $env:USERPROFILE '.platformio\packages\tool-mklittlefs\mklittlefs.exe'
$bootApp = Join-Path $env:USERPROFILE '.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin'
$variant = switch ($Environment) {
    'espwroom32_test' { 'test' }
    'espwroom32_isolation' { 'isolation' }
    default { 'full' }
}

if ($Target -eq 'ESPHome' -and $Environment -eq 'espwroom32_isolation') {
    throw 'The isolation environment is available only for Stand-alone firmware.'
}

function Assert-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required build artifact was not found: $Path"
    }
}

function Invoke-EsptoolMerge([string]$Output, [string[]]$Parts) {
    $arguments = @(
        $platformioEsptool,
        '--chip', 'esp32', 'merge_bin',
        '-o', $Output,
        '--flash_mode', 'dio',
        '--flash_freq', '40m',
        '--flash_size', '4MB',
        '--fill-flash-size', '4MB'
    ) + $Parts
    & $platformioPython @arguments
    if ($LASTEXITCODE -ne 0) { throw "Failed to create $Output" }
    if ((Get-Item -LiteralPath $Output).Length -ne 0x400000) {
        throw "Complete image is not exactly 4 MB: $Output"
    }
}

if (-not (Test-Path -LiteralPath $platformio -PathType Leaf)) {
    throw 'PlatformIO was not found. Install the PlatformIO VS Code extension first.'
}
if ($Target -eq 'ESPHome' -and -not (Test-Path -LiteralPath $esphomePython -PathType Leaf)) {
    throw 'ESPHome is not set up. Run the matching Build-ESPHome command again after setup completes.'
}

Push-Location $root
try {
    if ($Target -eq 'Standalone') {
        & $platformio run -e $Environment
        if ($LASTEXITCODE -ne 0) { throw 'Standalone firmware build failed.' }
    } else {
        # Installs PlatformIO's filesystem tool without compiling standalone firmware.
        & $platformio run -e $Environment -t buildfs
        if ($LASTEXITCODE -ne 0) { throw 'PlatformIO filesystem tool setup failed.' }
    }

    foreach ($requiredTool in @($platformioPython, $platformioEsptool, $mkLittleFs)) {
        Assert-File $requiredTool
    }

    $platformioBuild = Join-Path $root ".pio\build\$Environment"
    $stagedData = Join-Path $root ".cache\release-data-$variant"
    $resolvedStage = [IO.Path]::GetFullPath($stagedData)
    $expectedPrefix = [IO.Path]::GetFullPath((Join-Path $root '.cache')) + '\'
    if (-not $resolvedStage.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace unexpected staging path: $resolvedStage"
    }
    if (Test-Path -LiteralPath $stagedData) {
        Remove-Item -LiteralPath $stagedData -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stagedData -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $root 'data') -Force |
        Copy-Item -Destination $stagedData -Recurse -Force
    $invalidFixture = Join-Path $stagedData 'songs\broken.rttl'
    if (Test-Path -LiteralPath $invalidFixture) {
        Remove-Item -LiteralPath $invalidFixture -Force
    }

    $filesystem = Join-Path $platformioBuild 'littlefs.bin'
    & $mkLittleFs -c $stagedData -s 851968 -p 256 -b 4096 $filesystem
    if ($LASTEXITCODE -ne 0) { throw 'LittleFS release build failed.' }
    if ((Get-Item -LiteralPath $filesystem).Length -ne 0xD0000) {
        throw 'LittleFS image is not exactly 0xD0000 bytes.'
    }

    $releaseDirectory = Join-Path $root 'Releases'
    New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null

    if ($Target -eq 'Standalone') {
        $bootloader = Join-Path $platformioBuild 'bootloader.bin'
        $partitions = Join-Path $platformioBuild 'partitions.bin'
        $standaloneOta = Join-Path $platformioBuild 'firmware.bin'
        foreach ($artifact in @($bootloader, $partitions, $standaloneOta, $bootApp)) {
            Assert-File $artifact
        }

        $standaloneFullName = if ($variant -eq 'isolation') {
            'ShelfClock-Isolation-Full-Firmware.bin'
        } else {
            'ShelfClock-Full-Firmware.bin'
        }
        $standaloneUpdateName = if ($variant -eq 'isolation') {
            'ShelfClock-Isolation-OTA-Update.bin'
        } else {
            'ShelfClock-OTA-Update.bin'
        }
        $standaloneFull = Join-Path $releaseDirectory $standaloneFullName
        $standaloneUpdate = Join-Path $releaseDirectory $standaloneUpdateName
        Invoke-EsptoolMerge $standaloneFull @(
            '0x1000', $bootloader,
            '0x8000', $partitions,
            '0xe000', $bootApp,
            '0x10000', $standaloneOta,
            '0x330000', $filesystem
        )
        Copy-Item -LiteralPath $standaloneOta -Destination $standaloneUpdate -Force
        $outputs = @($standaloneFull, $standaloneUpdate)
    } else {
        & $esphomePython tools\run_esphome_probe.py --target runtime --variant $variant
        if ($LASTEXITCODE -ne 0) { throw 'ESPHome firmware build failed.' }

        $esphomeBuild = Join-Path $root '.cache\esphome\build\shelfclock\build'
        $esphomeFactory = Join-Path $esphomeBuild 'firmware.factory.bin'
        $esphomeOta = Join-Path $esphomeBuild 'firmware.ota.bin'
        foreach ($artifact in @($esphomeFactory, $esphomeOta)) {
            Assert-File $artifact
        }

        $esphomeFull = Join-Path $releaseDirectory 'ShelfClock-ESPHome-Full-Firmware.bin'
        $esphomeUpdate = Join-Path $releaseDirectory 'ShelfClock-OTA-ESPHome.bin'
        Invoke-EsptoolMerge $esphomeFull @(
            '0x0', $esphomeFactory,
            '0x330000', $filesystem
        )
        Copy-Item -LiteralPath $esphomeOta -Destination $esphomeUpdate -Force
        $outputs = @($esphomeFull, $esphomeUpdate)
    }

    Write-Host "Created two ShelfClock $Target release images from the $variant build:"
    Get-Item -LiteralPath $outputs | Select-Object Name, Length
} finally {
    Pop-Location
}
