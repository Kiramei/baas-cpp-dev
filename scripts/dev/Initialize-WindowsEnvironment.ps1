[CmdletBinding()]
param(
    [string]$PythonLauncher = "py",
    [switch]$SkipDependencyInstall
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$venvRoot = Join-Path $repoRoot ".venv"
$venvPython = Join-Path $venvRoot "Scripts\python.exe"
$requirements = Join-Path $PSScriptRoot "requirements-foundation.txt"
$conanHome = Join-Path $repoRoot ".conan2"

if (-not (Test-Path -LiteralPath $venvPython)) {
    $launcher = Get-Command $PythonLauncher -ErrorAction SilentlyContinue
    if (-not $launcher) {
        throw "Python launcher '$PythonLauncher' was not found; install Python 3.11 first."
    }
    if ($PythonLauncher -eq "py") {
        & $launcher.Source -3.11 -m venv $venvRoot
    } else {
        & $launcher.Source -m venv $venvRoot
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create the repository Python environment."
    }
}

$pythonVersion = & $venvPython -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
if ($LASTEXITCODE -ne 0 -or $pythonVersion.Trim() -ne "3.11") {
    throw "The repository environment must use Python 3.11; found '$pythonVersion'."
}

New-Item -ItemType Directory -Force -Path $conanHome | Out-Null

if (-not $SkipDependencyInstall) {
    & $venvPython -m pip install --disable-pip-version-check --upgrade pip
    if ($LASTEXITCODE -ne 0) {
        throw "Could not update pip in the repository environment."
    }
    & $venvPython -m pip install --disable-pip-version-check -r $requirements
    if ($LASTEXITCODE -ne 0) {
        throw "Could not install the foundation requirements."
    }
}

Write-Host "BAAS repository environment is initialized."
Write-Host "Repository: $repoRoot"
Write-Host "Python: $venvPython"
Write-Host "Conan home: $conanHome"
Write-Host "Run: . .\scripts\dev\Enter-WindowsDevShell.ps1"
