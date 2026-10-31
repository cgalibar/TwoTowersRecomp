# Dot-source to load the Visual Studio 2022+ x64 developer environment into the current session:
#   . tools\vsenv.ps1
# Set TTR_VCVARS64 to a vcvars64.bat path to override discovery.

function Import-TtrVsEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }

    $vcvars = $env:TTR_VCVARS64
    if (-not $vcvars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; run tools\setup_windows.ps1 first.' }
        $vsPath = & $vswhere -latest -products * -version '[17.0,)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vsPath) { throw 'Visual Studio 2022+ with the C++ x64 toolset was not found.' }
        $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    }

    $lines = & cmd.exe /s /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        $idx = $line.IndexOf('=')
        if ($idx -gt 0) { Set-Item -Path "env:$($line.Substring(0, $idx))" -Value $line.Substring($idx + 1) }
    }

    # Prefer the standalone LLVM install for clang-cl when present.
    $llvmBin = if ($env:LLVM_ROOT) { Join-Path $env:LLVM_ROOT 'bin' } else { Join-Path $env:ProgramFiles 'LLVM\bin' }
    if (Test-Path $llvmBin) { $env:PATH = "$llvmBin;$env:PATH" }

    foreach ($tool in 'CMake\bin', 'Ninja') {
        $dir = Join-Path $env:ProgramFiles $tool
        if (Test-Path $dir) { $env:PATH = "$env:PATH;$dir" }
    }
}

Import-TtrVsEnvironment
