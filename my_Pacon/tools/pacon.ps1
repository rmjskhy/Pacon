param(
    [ValidateSet('check', 'test', 'build', 'flash', 'build-flash', 'monitor', 'android-build', 'android-install')]
    [string]$Action = 'check',
    [string]$Port = 'COM11',
    [ValidateRange(1, 32)]
    [int]$Jobs = 24
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent $projectRoot
$buildDir = Join-Path $projectRoot 'build_wifi_fix'
$idfPath = 'D:\espidf\v5.4\v5.4\esp-idf'
$toolsPath = 'D:\espidf\mytools'
$pythonEnv = Join-Path $toolsPath 'python_env\idf5.4_py3.11_env'
$python = Join-Path $pythonEnv 'Scripts\python.exe'
$ninja = Join-Path $toolsPath 'tools\ninja\1.12.1\ninja.exe'
$adb = 'D:\Android\Sdk\platform-tools\adb.exe'
$androidRoot = Join-Path $workspaceRoot 'PaconBleTool'
$gradle = Join-Path $androidRoot 'gradlew.bat'
$javaHome = 'C:\Program Files\Android\Android Studio\jbr'

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label is missing: $Path"
    }
}

function Invoke-Checked([string]$FilePath, [string[]]$Arguments, [string]$Label) {
    Write-Host "[$Label] $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Test-Environment {
    Require-Path $idfPath 'ESP-IDF v5.4 tree'
    Require-Path $python 'ESP-IDF Python environment'
    Require-Path $ninja 'Pinned Ninja executable'
    Require-Path (Join-Path $buildDir 'build.ninja') 'Canonical firmware build tree'
    Require-Path (Join-Path $buildDir 'flasher_args.json') 'Firmware flash manifest'
    $cache = Get-Content -LiteralPath (Join-Path $buildDir 'CMakeCache.txt') -Raw
    if ($cache -notmatch 'esp-14\.2\.0_20250730') {
        throw 'Canonical build tree is not pinned to the verified ESP 14.2.0 toolchain.'
    }
    Write-Host "PACON environment OK: IDF=5.4, build=build_wifi_fix, jobs=$Jobs, port=$Port"
}

function Invoke-SourceTests {
    $tests = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests') -Filter 'check_*.ps1' -File |
        Sort-Object Name
    foreach ($test in $tests) {
        Write-Host "[test] $($test.Name)"
        & $test.FullName
        # PowerShell scripts do not own $LASTEXITCODE.  Inspecting it here can
        # accidentally reuse the result of an unrelated native tool and turn a
        # passing source check into a false failure.
        if (-not $?) {
            throw "Source regression failed: $($test.Name)"
        }
    }
}

function Invoke-FirmwareBuild {
    Test-Environment
    $env:IDF_PATH = $idfPath
    $env:IDF_TOOLS_PATH = $toolsPath
    $env:IDF_PYTHON_ENV_PATH = $pythonEnv
    $toolchainBin = Join-Path $toolsPath 'tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin'
    Require-Path $toolchainBin 'ESP32-S3 toolchain binaries'
    if (($env:PATH -split ';') -notcontains $toolchainBin) {
        $env:PATH = "$toolchainBin;$env:PATH"
    }
    Invoke-Checked $ninja @('-C', $buildDir, '-j', "$Jobs") 'firmware build'
}

function Invoke-FirmwareFlash {
    Test-Environment
    if ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) {
        throw "Serial port $Port is not present. Put the board in download mode and reconnect it."
    }
    $manifest = Get-Content -LiteralPath (Join-Path $buildDir 'flasher_args.json') -Raw | ConvertFrom-Json
    $args = @('-m', 'esptool', '--chip', $manifest.extra_esptool_args.chip,
              '-p', $Port, '-b', '921600', '--before', $manifest.extra_esptool_args.before,
              '--after', $manifest.extra_esptool_args.after, 'write_flash')
    $args += @($manifest.write_flash_args)
    foreach ($entry in $manifest.flash_files.PSObject.Properties) {
        $args += $entry.Name
        $args += (Join-Path $buildDir ([string]$entry.Value))
    }
    Invoke-Checked $python $args 'firmware flash'
}

function Invoke-Monitor {
    Test-Environment
    $idf = Join-Path $idfPath 'tools\idf.py'
    $toolchainBin = Join-Path $toolsPath 'tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin'
    Require-Path $toolchainBin 'ESP32-S3 toolchain binaries'
    # idf.py launches idf_monitor as a child process and resolves its Python
    # interpreter from IDF_PYTHON_ENV_PATH.  Pin it to the same verified v5.4
    # environment used by this script instead of inheriting a stale global
    # .espressif path.
    $env:IDF_TOOLS_PATH = $toolsPath
    $env:IDF_PYTHON_ENV_PATH = $pythonEnv
    if (($env:PATH -split ';') -notcontains $toolchainBin) {
        $env:PATH = "$toolchainBin;$env:PATH"
    }
    Invoke-Checked $python @($idf, '-C', $projectRoot, '-B', $buildDir, '-p', $Port, 'monitor') 'serial monitor'
}

function Invoke-AndroidBuild {
    Require-Path $gradle 'Android Gradle wrapper'
    Require-Path $javaHome 'Android Studio JBR'
    $env:JAVA_HOME = $javaHome
    $env:ANDROID_HOME = 'D:\Android\Sdk'
    Push-Location $androidRoot
    try {
        Invoke-Checked $gradle @('--no-daemon', '--console=plain', 'assembleDebug') 'Android build'
    } finally {
        Pop-Location
    }
}

function Invoke-AndroidInstall {
    Require-Path $adb 'ADB'
    $apk = Join-Path $androidRoot 'app\build\outputs\apk\debug\app-debug.apk'
    Require-Path $apk 'Android debug APK'
    Invoke-Checked $adb @('install', '-r', $apk) 'Android install'
}

try {
    switch ($Action) {
        'check' { Test-Environment }
        'test' { Invoke-SourceTests }
        'build' { Invoke-FirmwareBuild }
        'flash' { Invoke-FirmwareFlash }
        'build-flash' { Invoke-FirmwareBuild; Invoke-FirmwareFlash }
        'monitor' { Invoke-Monitor }
        'android-build' { Invoke-AndroidBuild }
        'android-install' { Invoke-AndroidInstall }
    }
} catch {
    Write-Error "PACON $Action failed: $($_.Exception.Message)"
    exit 1
}
