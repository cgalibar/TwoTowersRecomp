# Imports SLES_512.56 into a headless Ghidra project (Emotion Engine language) and runs
# PS2Recomp's ExportPS2Functions.java to produce a TOML + CSV function map in out/ghidra/.
#   powershell -ExecutionPolicy Bypass -File tools\ghidra\export_functions.ps1 [-GhidraDir J:\DevTools\ghidra_12.1.4_PUBLIC]
# Requires JDK 21 and the ghidra-emotionengine-reloaded extension matching the Ghidra version.
param(
    [string]$GhidraDir = $(if ($env:GHIDRA_INSTALL_DIR) { $env:GHIDRA_INSTALL_DIR } else { 'J:\DevTools\ghidra_12.1.4_PUBLIC' }),
    [string]$PS2RecompDir = '',
    [switch]$Reimport
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $PS2RecompDir) { $PS2RecompDir = Join-Path $repo 'tools\PS2Recomp' }
$elf = Join-Path $repo 'TwoTowersRecompLib\private\disc\SLES_512.56'
$scriptDir = Join-Path $PS2RecompDir 'ps2xRecomp\tools\ghidra'
$outDir = Join-Path $repo 'out\ghidra'
$projectDir = Join-Path $outDir 'project'
$propsDir = Join-Path $outDir 'props'
$headless = Join-Path $GhidraDir 'support\analyzeHeadless.bat'

foreach ($p in $elf, $headless, (Join-Path $scriptDir 'ExportPS2Functions.java')) {
    if (-not (Test-Path $p)) { throw "Missing $p" }
}

# Ghidra 12 needs JDK 21; fall back to the Temurin install from setup_windows.ps1.
if (-not $env:JAVA_HOME) {
    $jdk = Get-ChildItem (Join-Path $env:ProgramFiles 'Eclipse Adoptium') -Directory -Filter 'jdk-21*' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $jdk) { throw 'JDK 21 not found; install EclipseAdoptium.Temurin.21.JDK or set JAVA_HOME.' }
    $env:JAVA_HOME = $jdk.FullName
}
$env:PATH = "$(Join-Path $env:JAVA_HOME 'bin');$env:PATH"
New-Item -ItemType Directory -Force $projectDir, $propsDir | Out-Null

$toml = Join-Path $outDir 'sles_51256.ghidra.toml'
$csv = Join-Path $outDir 'sles_51256.functions.csv'
# Non-ASCII characters in script answers do not survive Ghidra's property parsing, so the
# script writes into an ASCII-only temp folder and the results are moved afterwards.
$stage = Join-Path ([IO.Path]::GetTempPath()) 'TwoTowersRecomp-ghidra'
New-Item -ItemType Directory -Force $stage | Out-Null
$stageToml = Join-Path $stage (Split-Path -Leaf $toml)
$stageCsv = Join-Path $stage (Split-Path -Leaf $csv)
Remove-Item $toml, $csv, $stageToml, $stageCsv -ErrorAction SilentlyContinue
# Answers for the script's askFile(title, approveButtonText) prompts in headless mode;
# Ghidra looks them up as "<title> <approveButtonText>".
$props = "Choose output TOML config file Save = $($stageToml -replace '\\', '/')`n" +
         "Choose output CSV file Save = $($stageCsv -replace '\\', '/')`n"
[IO.File]::WriteAllText((Join-Path $propsDir 'ExportPS2Functions.properties'), $props, [Text.Encoding]::ASCII)

$common = @('-scriptPath', $scriptDir, '-propertiesPath', $propsDir, '-postScript', 'ExportPS2Functions.java')
$hasProgram = Test-Path (Join-Path $projectDir 'TwoTowers.rep')
if ($Reimport -or -not $hasProgram) {
    & $headless $projectDir TwoTowers -import $elf -overwrite `
        -processor 'r5900:LE:32:default' -loader ElfLoader @common
} else {
    & $headless $projectDir TwoTowers -process 'SLES_512.56' -noanalysis @common
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# analyzeHeadless exits 0 even when the post-script throws.
if (-not (Test-Path $stageCsv)) { throw "ExportPS2Functions.java did not produce $stageCsv; check the Ghidra log above." }
Move-Item -Force $stageCsv $csv
if (Test-Path $stageToml) { Move-Item -Force $stageToml $toml }

Write-Host "Function map: $csv"
Write-Host "Set general.ghidra_output in TwoTowersRecompLib/config/sles_51256.toml to use it."
