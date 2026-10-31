# Writes the PS2Recomp submodule changes that the existing patches do not cover yet as a new patch
# on top of the stack in tools/patches/PS2Recomp.
#   powershell -ExecutionPolicy Bypass -File tools\export_patch.ps1 -Name 0004-ps2xRuntime-something
param(
    [Parameter(Mandatory = $true)][string]$Name
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$submodule = Join-Path $repo 'tools\PS2Recomp'
$patchDir = Join-Path $PSScriptRoot 'patches\PS2Recomp'
$output = Join-Path $patchDir ($Name + '.patch')
if (Test-Path $output) { throw "$output already exists" }

# git reports progress on stderr, which PowerShell 5.1 would turn into terminating errors.
function Invoke-Git([string]$Directory, [string[]]$GitArgs) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $text = & git -C $Directory @GitArgs 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($code -ne 0) { throw "git $($GitArgs -join ' ') failed ($code): $text" }
    return $text
}

# A scratch worktree (ASCII temp path) at the pinned commit with the current stack applied.
$base = Join-Path ([IO.Path]::GetTempPath()) ('ps2recomp-base-' + [guid]::NewGuid().ToString('N'))
Invoke-Git $submodule @('worktree', 'add', '--detach', $base, 'HEAD') | Out-Null
try {
    foreach ($patch in Get-ChildItem $patchDir -Filter '*.patch' | Sort-Object Name) {
        Invoke-Git $base @('apply', '--whitespace=nowarn', $patch.FullName) | Out-Null
    }
    Invoke-Git $base @('add', '-A') | Out-Null

    # Copy every changed file of the real submodule over it; what is left is the new patch.
    $changed = Invoke-Git $submodule @('status', '--porcelain', '--untracked-files=all')
    foreach ($line in $changed) {
        $file = "$line".Substring(3)
        $source = Join-Path $submodule $file
        $target = Join-Path $base $file
        if (Test-Path $source) {
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            Copy-Item $source $target -Force
        }
        elseif (Test-Path $target) {
            Remove-Item $target -Force
        }
    }
    Invoke-Git $base @('add', '-N', '.') | Out-Null
    Invoke-Git $base @('diff', '--output', $output) | Out-Null
    if ((Get-Item $output).Length -eq 0) {
        Remove-Item $output
        Write-Host 'No changes outside the existing patches.'
        return
    }
    Write-Host "wrote $output"
    Invoke-Git $base @('diff', '--stat') | ForEach-Object { Write-Host $_ }
}
finally {
    Invoke-Git $submodule @('worktree', 'remove', '--force', $base) | Out-Null
}
