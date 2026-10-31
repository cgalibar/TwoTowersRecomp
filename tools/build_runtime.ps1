# Configures and builds TwoTowersRecomp with a CMake preset.
#   powershell -ExecutionPolicy Bypass -File tools\build_runtime.ps1 [-Preset win-clang-debug] [-Clean] [-SkipPatches]
# -SkipPatches builds the submodule as it is (while editing files that the patches also touch).
param(
    [string]$Preset = 'win-clang-debug',
    [switch]$Clean,
    [switch]$SkipPatches,
    [string[]]$ExtraArgs = @()
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'vsenv.ps1')
if (-not $SkipPatches) { & (Join-Path $PSScriptRoot 'apply_patches.ps1') }

Push-Location $repo
try {
    if ($Clean) { Remove-Item -Recurse -Force (Join-Path $repo "out\build\$Preset") -ErrorAction SilentlyContinue }
    & cmake --preset $Preset @ExtraArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}
