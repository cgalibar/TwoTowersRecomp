# Builds the PS2Recomp host tools (ps2_recomp, ps2_analyzer) into out/build/tools.
#   powershell -ExecutionPolicy Bypass -File tools\build_tools.ps1 [-Compiler msvc|clang] [-Config Release]
param(
    [ValidateSet('msvc', 'clang')][string]$Compiler = 'msvc',
    [string]$Config = 'Release',
    [string]$PS2RecompDir = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'vsenv.ps1')

if (-not $PS2RecompDir) { $PS2RecompDir = Join-Path $repo 'tools\PS2Recomp' }
if (-not (Test-Path (Join-Path $PS2RecompDir 'CMakeLists.txt'))) {
    throw "PS2Recomp not found at $PS2RecompDir. Run: git submodule update --init --recursive"
}

$cc = if ($Compiler -eq 'clang') { 'clang-cl' } else { 'cl' }
$buildDir = Join-Path $repo 'out\build\tools'

& cmake -S $PS2RecompDir -B $buildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_C_COMPILER=$cc" "-DCMAKE_CXX_COMPILER=$cc" `
    -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON `
    -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $buildDir --target ps2_recomp ps2_analyzer
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Get-ChildItem $buildDir -Recurse -Include 'ps2_recomp.exe', 'ps2_analyzer.exe' | ForEach-Object { $_.FullName }
