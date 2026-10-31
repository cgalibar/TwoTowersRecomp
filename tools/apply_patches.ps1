# Applies tools/patches/PS2Recomp/*.patch to the PS2Recomp submodule. Safe to run repeatedly:
# the patches form one stack (later ones build on earlier ones in the same files), so it is
# either applied as a whole or skipped as a whole.
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

$patches = @(Get-ChildItem $patchDir -Filter '*.patch' | Sort-Object Name)
if ($patches.Count -eq 0) { return }

# The newest patch only reverses cleanly on top of the whole stack.
if ((Invoke-Git @('apply', '--check', '--reverse', $patches[-1].FullName)) -eq 0) {
    Write-Host "already applied: $($patches.Count) patches"
    return
}

# Otherwise apply from the first patch that still fits (a fresh checkout starts at the first one;
# a tree that already has the older patches picks up the new ones), each on top of the previous.
$first = 0
while ($first -lt $patches.Count -and
       (Invoke-Git @('apply', '--check', $patches[$first].FullName)) -ne 0) {
    $first++
}
if ($first -ge $patches.Count) {
    throw "Could not apply the PS2Recomp patches. Reset the submodule (git -C tools\PS2Recomp checkout . and clean -fd) and retry."
}
foreach ($patch in $patches[$first..($patches.Count - 1)]) {
    if ((Invoke-Git @('apply', '--whitespace=nowarn', $patch.FullName)) -ne 0) {
        throw "Could not apply $($patch.Name). Reset the submodule (git -C tools\PS2Recomp checkout . and clean -fd) and retry."
    }
    Write-Host "applied: $($patch.Name)"
}
