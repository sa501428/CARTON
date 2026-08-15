[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required tool '$Name' was not found in PATH."
    }
}

Require-Command "cmake"
Require-Command "cpack"
Require-Command "makensis"

$SourceDir = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $SourceDir "build-carton-windows"
}
$Logo = Join-Path $SourceDir "logo.svg"
if (-not (Test-Path -LiteralPath $Logo -PathType Leaf)) {
    throw "Logo source is missing: $Logo"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host "-> Configuring CARTON..."
& cmake -S $SourceDir -B $BuildDir `
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF @CMakeArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

Write-Host "-> Building CARTON and generating carton.ico from logo.svg..."
& cmake --build $BuildDir --config Release --target carton --parallel
if ($LASTEXITCODE -ne 0) {
    throw "CARTON build failed with exit code $LASTEXITCODE."
}

# Exercise the same install/deployment rules that CPack uses. This catches a
# missing Qt/QML plugin or unresolved third-party DLL before creating an installer.
$PackageDir = Join-Path $BuildDir "package-smoke"
if (Test-Path -LiteralPath $PackageDir) {
    Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
Write-Host "-> Deploying and smoke-testing the packaged application..."
& cmake --install $BuildDir --config Release --prefix $PackageDir
if ($LASTEXITCODE -ne 0) {
    throw "CARTON deployment failed with exit code $LASTEXITCODE."
}

$PackagedExe = Join-Path $PackageDir "CARTON.exe"
$PlatformPlugin = Join-Path $PackageDir "platforms\qwindows.dll"
if (-not (Test-Path -LiteralPath $PackagedExe -PathType Leaf)) {
    throw "The deployed application is missing: $PackagedExe"
}
if (-not (Test-Path -LiteralPath $PlatformPlugin -PathType Leaf)) {
    throw "The deployed application is missing the Qt Windows platform plugin: $PlatformPlugin"
}

$PreviousQpaPlatform = $env:QT_QPA_PLATFORM
$PreviousRhiBackend = $env:QSG_RHI_BACKEND
try {
    $env:QT_QPA_PLATFORM = "offscreen"
    $env:QSG_RHI_BACKEND = "software"
    & $PackagedExe --smoke-test
    if ($LASTEXITCODE -ne 0) {
        throw "The packaged application smoke test failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:QT_QPA_PLATFORM = $PreviousQpaPlatform
    $env:QSG_RHI_BACKEND = $PreviousRhiBackend
}

Write-Host "-> Creating the NSIS installer..."
& cpack --config (Join-Path $BuildDir "CPackConfig.cmake") `
    -C Release -G NSIS -B $BuildDir
if ($LASTEXITCODE -ne 0) {
    throw "NSIS packaging failed with exit code $LASTEXITCODE."
}

$Installers = @(Get-ChildItem -LiteralPath $BuildDir -Filter "CARTON-*-Windows.exe" -File |
    Sort-Object LastWriteTime -Descending)
if ($Installers.Count -eq 0) {
    throw "CPack completed but no CARTON Windows installer was found in $BuildDir."
}

Write-Host "Installer ready: $($Installers[0].FullName)"
