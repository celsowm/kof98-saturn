param(
    [Parameter(Mandatory=$true)][string]$Image,
    [Parameter(Mandatory=$true)][string]$Out,
    [string[]]$Addresses = @('0xC11002'),
    [string]$GhidraDir = 'C:\Users\celso\AppData\Local\Temp\opencode\ghidra\ghidra_12.2_PUBLIC'
)

$ErrorActionPreference = 'Stop'
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-25.0.5.8-hotspot'
$headless = Join-Path $GhidraDir 'support\analyzeHeadless.bat'
$projectDir = Join-Path $env:TEMP 'kof98_ghidra_project'
New-Item -ItemType Directory -Force -Path $projectDir | Out-Null
$scriptDir = (Resolve-Path (Join-Path $PSScriptRoot '.')).Path

# One-shot: import the raw image, auto-analyze, run the decompiler script.
& $headless $projectDir kof98 -import $Image -processor 68000:BE:32:default `
    -loader BinaryLoader -loader-baseAddr 0 -overwrite `
    -scriptPath $scriptDir -postScript GhidraDecompile.java $Out @Addresses 2>&1 |
    Select-Object -Last 20
