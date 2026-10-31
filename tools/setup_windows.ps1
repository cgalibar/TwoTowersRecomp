# Installs the Windows toolchain used by TwoTowersRecomp.
# Run from an elevated PowerShell (or let the script relaunch itself elevated):
#   powershell -ExecutionPolicy Bypass -File tools\setup_windows.ps1
param(
    [string]$LogFile = "$env:TEMP\TwoTowersRecomp-setup.log",
    [switch]$SkipVisualStudio,
    [switch]$SkipOptional
)

$ErrorActionPreference = 'Continue'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $argsList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-LogFile', "`"$LogFile`"")
    if ($SkipVisualStudio) { $argsList += '-SkipVisualStudio' }
    if ($SkipOptional) { $argsList += '-SkipOptional' }
    Start-Process powershell -Verb RunAs -Wait -ArgumentList $argsList
    exit
}

Start-Transcript -Path $LogFile -Append | Out-Null

function Install-WingetPackage([string]$Id, [string]$Override = $null) {
    Write-Host "==> $Id"
    $wingetArgs = @('install', '--id', $Id, '-e', '--silent', '--accept-package-agreements',
                    '--accept-source-agreements', '--disable-interactivity')
    if ($Override) { $wingetArgs += @('--override', $Override) }
    & winget @wingetArgs
    Write-Host "<== $Id exit=$LASTEXITCODE"
}

# Required: build system and compilers.
Install-WingetPackage 'Kitware.CMake'
Install-WingetPackage 'Ninja-build.Ninja'
Install-WingetPackage 'LLVM.LLVM'

if (-not $SkipVisualStudio) {
    # MSVC toolset + Windows SDK. clang-cl links against these headers/libs.
    $vsOverride = '--wait --passive --norestart ' +
                  '--add Microsoft.VisualStudio.Workload.VCTools ' +
                  '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
                  '--add Microsoft.VisualStudio.Component.Windows11SDK.26100 ' +
                  '--add Microsoft.VisualStudio.Component.VC.CMake.Project ' +
                  '--add Microsoft.VisualStudio.Component.VC.ATL'
    Install-WingetPackage 'Microsoft.VisualStudio.2022.BuildTools' $vsOverride
}

# Required for the Ghidra function-boundary workflow.
Install-WingetPackage 'EclipseAdoptium.Temurin.21.JDK'

if (-not $SkipOptional) {
    Install-WingetPackage '7zip.7zip'          # disc image inspection
    Install-WingetPackage 'PCSX2Team.PCSX2'    # reference behaviour / debugger
    Install-WingetPackage 'GitHub.cli'         # publishing
}

Write-Host 'Toolchain setup finished.'
Stop-Transcript | Out-Null
