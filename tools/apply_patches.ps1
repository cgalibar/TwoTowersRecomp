# Applies tools/patches/PS2Recomp/*.patch to the PS2Recomp submodule. Safe to run repeatedly:
# patches that are already applied are skipped.
#   powershell -ExecutionPolicy Bypass -File tools\apply_patches.ps1 [-PS2RecompDir path]
param(
    [string]$PS2RecompDir = ''
)

$repo = Split-Path -Parent $PSScriptRoot
if (-not $PS2RecompDir) { $PS2RecompDir = Join-Path $repo 'tools\PS2Recomp' }
$patchDir = Join-Path $PSScriptRoot 'patches\PS2Recomp'

if (-not (Test-Path (Join-Path $PS2RecompDir 'CMakeLists.txt'))) {
    throw "PS2Recomp not found at $PS2RecompDir. Run: git submodule update --init --recursive"
}

# git reports "patch does not apply" on stderr; only exit codes matter here (PowerShell 5.1 would
# otherwise turn that stderr into a terminating error).
function Invoke-Git([string[]]$GitArgs) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & git -C $PS2RecompDir @GitArgs 2>&1 | Out-Null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    return $code
}

foreach ($patch in Get-ChildItem $patchDir -Filter '*.patch' | Sort-Object Name) {
    if ((Invoke-Git @('apply', '--check', '--reverse', $patch.FullName)) -eq 0) {
        Write-Host "already applied: $($patch.Name)"
        continue
    }
    if ((Invoke-Git @('apply', '--whitespace=nowarn', $patch.FullName)) -ne 0) {
        throw "Could not apply $($patch.Name). Reset the submodule (git -C tools\PS2Recomp checkout .) and retry."
    }
    Write-Host "applied: $($patch.Name)"
}
