[CmdletBinding()]
param(
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

function Assert-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required build artifact was not found: $Path"
    }
}

function Convert-PioSection([object[]]$Config, [string]$Name) {
    $section = $Config | Where-Object { [string]$_[0] -eq $Name } | Select-Object -First 1
    if ($null -eq $section) {
        throw "PlatformIO section [$Name] was not found."
    }
    $values = @{}
    foreach ($entry in $section[1]) {
        $values[[string]$entry[0]] = $entry[1]
    }
    return $values
}

function Get-RequiredSetting([hashtable]$Settings, [string]$Name) {
    if (-not $Settings.ContainsKey($Name) -or [string]::IsNullOrWhiteSpace([string]$Settings[$Name])) {
        throw "The active PlatformIO environment must define $Name."
    }
    return [string]$Settings[$Name]
}

function Convert-ByteValue([string]$Value, [string]$Name) {
    try {
        if ($Value.StartsWith('0x', [StringComparison]::OrdinalIgnoreCase)) {
            return [Convert]::ToInt64($Value.Substring(2), 16)
        }
        return [Convert]::ToInt64($Value, 10)
    } catch {
        throw "$Name must be a decimal or hexadecimal byte value, not '$Value'."
    }
}

function Get-DefineValue([string[]]$Flags, [string]$Name, [string]$Default = $null) {
    $pattern = '^\s*-D\s+' + [Regex]::Escape($Name) + '(?:=(.+))?\s*$'
    foreach ($flag in $Flags) {
        if ($flag -match $pattern) {
            if ([string]::IsNullOrWhiteSpace($Matches[1])) {
                return 'true'
            }
            return $Matches[1].Trim().Trim('"')
        }
    }
    return $Default
}

function Get-BoolDefine([string[]]$Flags, [string]$Name) {
    $value = Get-DefineValue $Flags $Name
    if ($null -eq $value) {
        throw "The shared build_flags must explicitly define $Name."
    }
    switch ($value.ToLowerInvariant()) {
        { $_ -in @('true', '1', 'yes', 'on') } { return $true }
        { $_ -in @('false', '0', 'no', 'off') } { return $false }
        default { throw "$Name must be true or false, not '$value'." }
    }
}

function Get-BoolText([bool]$Value) {
    if ($Value) { return 'true' }
    return 'false'
}

function Invoke-EsptoolMerge([string]$Output, [string[]]$Parts,
                             [string]$FlashSize, [long]$FlashBytes) {
    $arguments = @(
        $platformioEsptool,
        '--chip', 'esp32', 'merge_bin',
        '-o', $Output,
        '--flash_mode', 'dio',
        '--flash_freq', '40m',
        '--flash_size', $FlashSize,
        '--fill-flash-size', $FlashSize
    ) + $Parts
    & $platformioPython @arguments
    if ($LASTEXITCODE -ne 0) { throw "Failed to create $Output" }
    if ((Get-Item -LiteralPath $Output).Length -ne $FlashBytes) {
        throw "Complete image is not exactly $FlashBytes bytes: $Output"
    }
}

Assert-File $platformio

Push-Location $root
try {
    $configJson = & $platformio project config --json-output
    if ($LASTEXITCODE -ne 0) { throw 'Could not read platformio.ini.' }
    $config = $configJson | ConvertFrom-Json
    $platformSettings = Convert-PioSection $config 'platformio'
    $environments = @($platformSettings['default_envs'])
    if ($environments.Count -ne 1) {
        throw 'platformio.default_envs must select exactly one hardware environment.'
    }
    $environment = [string]$environments[0]
    $environmentSettings = Convert-PioSection $config "env:$environment"
    $buildFlags = @($environmentSettings['build_flags'] | ForEach-Object { [string]$_ })

    $testClock = Get-BoolDefine $buildFlags 'TEST_CLOCK'
    $isolation = Get-BoolDefine $buildFlags 'SHELFCLOCK_ISOLATION'
    $featureNames = @(
        'HAS_ONLINEWEATHER',
        'HAS_USWEATHER',
        'HAS_RTC',
        'HAS_DHT',
        'HAS_SOUNDDETECTOR',
        'HAS_BUZZER',
        'HAS_PHOTOSENSOR'
    )
    $features = @{}
    foreach ($name in $featureNames) {
        $features[$name] = Get-BoolDefine $buildFlags $name
    }
    $flashLock = Get-DefineValue $buildFlags 'FASTLED_ESP32_FLASH_LOCK' '0'
    $ledsPerSegmentText = Get-DefineValue $buildFlags 'LEDS_PER_SEGMENT'
    $ledsPerSegment = if ([string]::IsNullOrWhiteSpace($ledsPerSegmentText)) {
        if ($testClock) { 4 } else { 7 }
    } else {
        [int]$ledsPerSegmentText
    }
    if ($ledsPerSegment -lt 1 -or $ledsPerSegment -gt 10) {
        throw 'LEDS_PER_SEGMENT must be between 1 and 10.'
    }

    if ($Target -eq 'ESPHome' -and $isolation) {
        throw 'SHELFCLOCK_ISOLATION=true is Standalone-only. Set it to false before building ESPHome.'
    }
    if ($isolation -and ($features['HAS_ONLINEWEATHER'] -or $features['HAS_USWEATHER'])) {
        Write-Warning 'Isolation has no internet route; disable both weather flags for the intended isolation build.'
    }

    $flashSize = Get-RequiredSetting $environmentSettings 'custom_flash_size'
    $flashBytes = Convert-ByteValue (Get-RequiredSetting $environmentSettings 'custom_flash_bytes') 'custom_flash_bytes'
    $bootloaderOffset = Get-RequiredSetting $environmentSettings 'custom_bootloader_offset'
    $partitionsOffset = Get-RequiredSetting $environmentSettings 'custom_partitions_offset'
    $bootAppOffset = Get-RequiredSetting $environmentSettings 'custom_boot_app_offset'
    $applicationOffset = Get-RequiredSetting $environmentSettings 'custom_application_offset'
    $filesystemOffset = Get-RequiredSetting $environmentSettings 'custom_filesystem_offset'
    $filesystemSizeText = Get-RequiredSetting $environmentSettings 'custom_filesystem_size'
    $filesystemSize = Convert-ByteValue $filesystemSizeText 'custom_filesystem_size'
    $partitionConfig = Get-RequiredSetting $environmentSettings 'board_build.partitions'
    $partitionFile = Join-Path $root $partitionConfig
    Assert-File $partitionFile

    $filesystemPartition = Get-Content -LiteralPath $partitionFile |
        Where-Object { $_ -notmatch '^\s*#' -and $_ -match ',\s*data\s*,\s*spiffs\s*,' } |
        Select-Object -First 1
    if ($null -eq $filesystemPartition) {
        throw "No data/spiffs filesystem partition was found in $partitionConfig."
    }
    $partitionFields = @($filesystemPartition.Split(',') | ForEach-Object { $_.Trim() })
    $partitionFilesystemOffset = Convert-ByteValue $partitionFields[3] 'filesystem partition offset'
    $partitionFilesystemSize = Convert-ByteValue $partitionFields[4] 'filesystem partition size'
    if ($partitionFilesystemOffset -ne (Convert-ByteValue $filesystemOffset 'custom_filesystem_offset') -or
        $partitionFilesystemSize -ne $filesystemSize) {
        throw 'The custom filesystem offset/size does not match the selected partition CSV.'
    }

    $variant = if ($testClock) { 'test' } else { 'full' }
    $networkFlavor = if ($isolation) { 'isolation' } else { 'normal network' }
    Write-Host ''
    Write-Host "ShelfClock $Target build"
    Write-Host "  Environment:       $environment"
    Write-Host "  Wiring map:        $variant"
    Write-Host "  LEDs per segment:  $ledsPerSegment"
    Write-Host "  Network:           $networkFlavor"
    Write-Host "  Flash layout:      $flashSize, $partitionConfig"
    foreach ($name in $featureNames) {
        Write-Host ("  {0,-19}{1}" -f ($name + ':'), (Get-BoolText $features[$name]))
    }
    Write-Host ''

    if ($Target -eq 'ESPHome' -and -not (Test-Path -LiteralPath $esphomePython -PathType Leaf)) {
        & (Join-Path $PSScriptRoot 'setup_esphome.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'ESPHome setup failed.' }
    }

    if ($Target -eq 'Standalone') {
        & $platformio run -e $environment
        if ($LASTEXITCODE -ne 0) { throw 'Standalone firmware build failed.' }
    } else {
        # Installs PlatformIO's filesystem tool without compiling the Standalone host.
        & $platformio run -e $environment -t buildfs
        if ($LASTEXITCODE -ne 0) { throw 'PlatformIO filesystem tool setup failed.' }
    }

    foreach ($requiredTool in @($platformioPython, $platformioEsptool, $mkLittleFs)) {
        Assert-File $requiredTool
    }

    $platformioBuild = Join-Path $root ".pio\build\$environment"
    $stagedData = Join-Path $root ".cache\release-data-$environment"
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
    & $mkLittleFs -c $stagedData -s $filesystemSize -p 256 -b 4096 $filesystem
    if ($LASTEXITCODE -ne 0) { throw 'LittleFS release build failed.' }
    if ((Get-Item -LiteralPath $filesystem).Length -ne $filesystemSize) {
        throw "LittleFS image is not exactly $filesystemSize bytes."
    }

    $releaseDirectory = Join-Path $root 'Releases'
    New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
    foreach ($retiredName in @(
        'ShelfClock-OTA-ESPHome.bin',
        'ShelfClock-Isolation-Full-Firmware.bin',
        'ShelfClock-Isolation-OTA-Update.bin'
    )) {
        $retiredPath = Join-Path $releaseDirectory $retiredName
        if (Test-Path -LiteralPath $retiredPath -PathType Leaf) {
            Remove-Item -LiteralPath $retiredPath -Force
        }
    }

    if ($Target -eq 'Standalone') {
        $bootloader = Join-Path $platformioBuild 'bootloader.bin'
        $partitions = Join-Path $platformioBuild 'partitions.bin'
        $standaloneOta = Join-Path $platformioBuild 'firmware.bin'
        foreach ($artifact in @($bootloader, $partitions, $standaloneOta, $bootApp)) {
            Assert-File $artifact
        }

        $standaloneFull = Join-Path $releaseDirectory 'ShelfClock-Full-Firmware.bin'
        $standaloneUpdate = Join-Path $releaseDirectory 'ShelfClock-OTA-Update.bin'
        Invoke-EsptoolMerge $standaloneFull @(
            $bootloaderOffset, $bootloader,
            $partitionsOffset, $partitions,
            $bootAppOffset, $bootApp,
            $applicationOffset, $standaloneOta,
            $filesystemOffset, $filesystem
        ) $flashSize $flashBytes
        Copy-Item -LiteralPath $standaloneOta -Destination $standaloneUpdate -Force
        $outputs = @($standaloneFull, $standaloneUpdate)
    } else {
        $environmentPackage = if ($features['HAS_DHT']) {
            'packages/environment-dht.yaml'
        } else {
            'packages/environment-disabled.yaml'
        }
        $probeArguments = @(
            'tools\run_esphome_probe.py',
            '--target', 'runtime',
            '--variant', $variant
        )
        $substitutions = [ordered]@{
            test_clock = Get-BoolText $testClock
            leds_per_segment = [string]$ledsPerSegment
            fastled_flash_lock = $flashLock
            has_onlineweather = Get-BoolText $features['HAS_ONLINEWEATHER']
            has_usweather = Get-BoolText $features['HAS_USWEATHER']
            has_rtc = Get-BoolText $features['HAS_RTC']
            has_dht = Get-BoolText $features['HAS_DHT']
            has_sounddetector = Get-BoolText $features['HAS_SOUNDDETECTOR']
            has_buzzer = Get-BoolText $features['HAS_BUZZER']
            has_photosensor = Get-BoolText $features['HAS_PHOTOSENSOR']
            flash_size = $flashSize
            partition_file = $partitionConfig.Replace('\', '/')
            environment_package = $environmentPackage
        }
        foreach ($entry in $substitutions.GetEnumerator()) {
            $probeArguments += @('--substitution', [string]$entry.Key, [string]$entry.Value)
        }
        & $esphomePython @probeArguments
        if ($LASTEXITCODE -ne 0) { throw 'ESPHome firmware build failed.' }

        $esphomeBuild = Join-Path $root '.cache\esphome\build\shelfclock\build'
        $esphomeFactory = Join-Path $esphomeBuild 'firmware.factory.bin'
        $esphomeOta = Join-Path $esphomeBuild 'firmware.ota.bin'
        foreach ($artifact in @($esphomeFactory, $esphomeOta)) {
            Assert-File $artifact
        }

        $esphomeFull = Join-Path $releaseDirectory 'ShelfClock-ESPHome-Full-Firmware.bin'
        $esphomeUpdate = Join-Path $releaseDirectory 'ShelfClock-ESPHome-OTA-Update.bin'
        Invoke-EsptoolMerge $esphomeFull @(
            '0x0', $esphomeFactory,
            $filesystemOffset, $filesystem
        ) $flashSize $flashBytes
        Copy-Item -LiteralPath $esphomeOta -Destination $esphomeUpdate -Force
        $outputs = @($esphomeFull, $esphomeUpdate)
    }

    Write-Host "Created two ShelfClock $Target release images from the $variant $networkFlavor configuration:"
    Get-Item -LiteralPath $outputs | Select-Object Name, Length
} finally {
    Pop-Location
}
