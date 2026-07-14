[CmdletBinding()]
param(
    [ValidateSet("all", "arm64-v8a", "x86_64")]
    [string]$Abi = "all",

    [ValidateRange(1, 32)]
    [int]$Jobs = 8,

    [string]$NdkPath = "",

    [switch]$SkipRecipeExport
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Push-Location $repoRoot
try {
. (Join-Path $PSScriptRoot "Enter-WindowsDevShell.ps1")

if ($NdkPath) {
    $resolvedNdk = (Resolve-Path -LiteralPath $NdkPath -ErrorAction Stop).Path
    $env:ANDROID_NDK_LATEST_HOME = $resolvedNdk
    $env:ANDROID_NDK_HOME = $resolvedNdk
}

if (-not $env:ANDROID_NDK_LATEST_HOME -or
    -not (Test-Path -LiteralPath $env:ANDROID_NDK_LATEST_HOME)) {
    throw "No Android NDK was discovered. Install an SDK NDK, pass -NdkPath, and reactivate the development shell."
}

foreach ($command in @("conan", "cmake", "ninja")) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command '$command' was not found in the activated development shell."
    }
}

if (-not $SkipRecipeExport) {
    & python deploy\conan\scripts\manage_recipes.py export
    if ($LASTEXITCODE -ne 0) {
        throw "Could not export the checked-in Conan recipes."
    }
}

$abis = if ($Abi -eq "all") { @("arm64-v8a", "x86_64") } else { @($Abi) }
$settings = @{
    "arm64-v8a" = @{
        Profile = "android-clang-arm64-v8a-release"
        Preset = "conan-android-clang-release-ocr-arm64-v8a"
        Machine = "AArch64"
    }
    "x86_64" = @{
        Profile = "android-clang-x86_64-release"
        Preset = "conan-android-clang-release-ocr-x86_64"
        Machine = "Advanced Micro Devices X86-64"
    }
}

$readelf = Join-Path $env:ANDROID_NDK_LATEST_HOME `
    "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe"
if (-not (Test-Path -LiteralPath $readelf)) {
    throw "NDK ELF inspector was not found: $readelf"
}

foreach ($targetAbi in $abis) {
    $target = $settings[$targetAbi]
    $conanOutput = "build/conan/android-clang-release-ocr-$targetAbi"
    $conanArgs = @(
        "install", "deploy/conan",
        "-of", $conanOutput,
        "-pr:h=deploy/conan/profiles/$($target.Profile)",
        "-pr:h=deploy/conan/profiles/dependency-versions-default",
        "-c:h=tools.android:ndk_path=$env:ANDROID_NDK_LATEST_HOME",
        "-o", "&:onnxruntime_use_cuda=False",
        "-o", "&:use_ffmpeg=False",
        "-o", "&:use_benchmark=False",
        "--build=missing",
        "--no-remote"
    )

    Write-Host "Building Android OCR for $targetAbi"
    & conan @conanArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Conan install failed for $targetAbi."
    }

    & cmake --preset $target.Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for $targetAbi."
    }

    & cmake --build --preset $target.Preset --parallel $Jobs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed for $targetAbi."
    }

    $libraryDir = Join-Path $repoRoot "build\$($target.Preset)\bin\lib\$targetAbi"
    $serverLibrary = Join-Path $libraryDir "libBAAS_ocr_server.so"
    if (-not (Test-Path -LiteralPath $serverLibrary)) {
        throw "Expected OCR library was not produced: $serverLibrary"
    }

    $elfHeader = (& $readelf -h $serverLibrary) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $elfHeader -notmatch [regex]::Escape($target.Machine)) {
        throw "OCR library has the wrong ELF machine for $targetAbi."
    }

    foreach ($runtimeLibrary in @(
        "libc++_shared.so",
        "libonnxruntime.so",
        "libopencv_world.so",
        "libsimdutf.so",
        "libspdlog.so"
    )) {
        $runtimePath = Join-Path $libraryDir $runtimeLibrary
        if (-not (Test-Path -LiteralPath $runtimePath)) {
            throw "Required runtime library was not packaged: $runtimePath"
        }
    }

    Write-Host "Verified $targetAbi OCR artifacts in $libraryDir"
}

Write-Host "Android OCR build completed. No emulator or device was started."
} finally {
    Pop-Location
}
