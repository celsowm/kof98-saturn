[CmdletBinding()]
param(
    [string]$LibSaturnRoot = "$env:USERPROFILE\Documents\projetos\libsaturn-1",
    [string]$MednafenExe,
    [string]$YmirExe
)
$ErrorActionPreference = 'Stop'
$launcher = Join-Path $LibSaturnRoot 'emulators\mednafen\run-mednafen.ps1'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw "Launcher not found: $launcher" }
if (-not $MednafenExe) {
    $cmd = Get-Command mednafen.exe -ErrorAction SilentlyContinue
    if ($cmd) { $MednafenExe = $cmd.Source }
    else {
        $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
        $MednafenExe = Get-ChildItem -LiteralPath $packages -Directory -Filter 'MednafenTeam.Mednafen_*' -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName 'mednafen.exe' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }
}
$dirs = @()
if ($MednafenExe) {
    if (-not (Test-Path -LiteralPath $MednafenExe -PathType Leaf)) { throw "Mednafen not found: $MednafenExe" }
    $dirs += Split-Path -Parent (Resolve-Path -LiteralPath $MednafenExe).Path
}
if ($YmirExe) {
    if (-not (Test-Path -LiteralPath $YmirExe -PathType Leaf)) { throw "Ymir not found: $YmirExe" }
    $dirs += Split-Path -Parent (Resolve-Path -LiteralPath $YmirExe).Path
}
foreach ($rel in @('emulators\kronos\kronos.exe','emulators\YabaSanshiro\yabasanshiro.exe','emulators\ymir\ymir.exe')) {
    $exe = Join-Path $LibSaturnRoot $rel
    if (Test-Path -LiteralPath $exe -PathType Leaf) { $dirs += Split-Path -Parent $exe }
}
$dirs = @($dirs | Select-Object -Unique)
# Preserve existing user PATH; process PATH is independently updated.
$userPath = [Environment]::GetEnvironmentVariable('Path','User')
$parts = @($userPath -split ';' | Where-Object { $_ })
foreach ($d in $dirs) { if ($parts -notcontains $d) { $parts += $d } }
[Environment]::SetEnvironmentVariable('Path',($parts -join ';'),'User')
foreach ($d in $dirs) { if (($env:Path -split ';') -notcontains $d) { $env:Path += ";$d" } }
$env:LIBSATURN_ROOT = (Resolve-Path -LiteralPath $LibSaturnRoot).Path
[Environment]::SetEnvironmentVariable('LIBSATURN_ROOT',$env:LIBSATURN_ROOT,'User')
# A short command that retains region/BIOS/cart policy of the SDK launcher.
$functionCode = @'
function global:saturn {
    param(
        [Parameter(Mandatory=$true,Position=0)][string]$GamePath,
        [ValidateSet('jp','na','eu','auto')][string]$Region='jp',
        [ValidateSet('auto','none','backup','extram1','extram4','cs1ram16')][string]$Cart='extram4'
    )
    $disc = (Resolve-Path -LiteralPath $GamePath -ErrorAction Stop).Path
    & (Join-Path $env:LIBSATURN_ROOT 'emulators\mednafen\run-mednafen.ps1') -GamePath $disc -Region $Region -Cart $Cart
}
function global:kofrun {
    param([ValidateSet('probe','frame','sequence')][string]$Mode='sequence')
    $names = @{probe='kof_graphics_probe';frame='kof_frame_replay';sequence='kof_sequence_replay'}
    saturn "build-saturn/disc/$($names[$Mode]).cue"
}
'@
$profilePath = $PROFILE.CurrentUserAllHosts
$parent = Split-Path -Parent $profilePath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$begin = '# BEGIN KOF SATURN EMULATOR SHORTCUTS'
$end = '# END KOF SATURN EMULATOR SHORTCUTS'
$existing = if (Test-Path -LiteralPath $profilePath) { Get-Content -LiteralPath $profilePath -Raw } else { '' }
if ($null -eq $existing) { $existing = '' }
if ($existing.Contains($begin) -ne $existing.Contains($end)) { throw "Incomplete managed block in $profilePath" }
$pattern = '(?ms)^' + [regex]::Escape($begin) + '\r?\n.*?^' + [regex]::Escape($end) + '\r?\n?'
$existing = [regex]::Replace($existing,$pattern,'')
if (Test-Path -LiteralPath $profilePath) { Copy-Item -LiteralPath $profilePath -Destination "$profilePath.kof-backup" -Force }
Set-Content -LiteralPath $profilePath -Encoding UTF8 -Value ($existing.TrimEnd()+"\r\n"+$begin+"\r\n"+$functionCode+"\r\n"+$end)
. ([scriptblock]::Create($functionCode))
Write-Host "Persistent emulator directories: $($dirs -join '; ')"
Write-Host 'From KOF root: kofrun probe | kofrun frame | kofrun sequence'
Write-Host 'Any CUE: saturn <path>'
