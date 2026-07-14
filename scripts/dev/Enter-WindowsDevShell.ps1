$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$venvScripts = Join-Path $repoRoot ".venv\Scripts"
$venvPython = Join-Path $venvScripts "python.exe"
$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $venvPython)) {
    throw "Repository Python environment is missing. Run .\scripts\dev\Initialize-WindowsEnvironment.ps1 first."
}
if (-not (Test-Path -LiteralPath $vsWhere)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}

$vsPath = & $vsWhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) {
    throw "Visual Studio 2022 C++ Build Tools were not found."
}

$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
$environmentLines = & cmd.exe /s /c "`"$devCmd`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

$cmakeRoot = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake"
$cmakeBin = Join-Path $cmakeRoot "CMake\bin"
$ninjaBin = Join-Path $cmakeRoot "Ninja"
$toolPaths = @($venvScripts, $cmakeBin, $ninjaBin)

$javaHome = $env:JAVA_HOME
if (-not $javaHome) {
    $javaHome = [Environment]::GetEnvironmentVariable("JAVA_HOME", "Machine")
}
if ($javaHome -and (Test-Path -LiteralPath (Join-Path $javaHome "bin\java.exe"))) {
    $env:JAVA_HOME = $javaHome
    $toolPaths += Join-Path $javaHome "bin"
}

$androidSdk = $env:ANDROID_SDK_ROOT
if (-not $androidSdk) {
    $androidSdk = $env:ANDROID_HOME
}
if (-not $androidSdk -and $env:LOCALAPPDATA) {
    $candidate = Join-Path $env:LOCALAPPDATA "Android\Sdk"
    if (Test-Path -LiteralPath $candidate) {
        $androidSdk = $candidate
    }
}
if ($androidSdk -and (Test-Path -LiteralPath $androidSdk)) {
    $env:ANDROID_SDK_ROOT = $androidSdk
    $toolPaths += @(
        (Join-Path $androidSdk "platform-tools"),
        (Join-Path $androidSdk "emulator"),
        (Join-Path $androidSdk "cmdline-tools\latest\bin")
    )
}

$env:Path = (($toolPaths | Where-Object { Test-Path -LiteralPath $_ }) -join ";") + ";$env:Path"
$env:CONAN_HOME = Join-Path $repoRoot ".conan2"
$env:BAAS_CPP_DEV_ROOT = $repoRoot

Write-Host "BAAS C++ development environment activated."
Write-Host "Repository: $repoRoot"
