# Puts the Switch build (out\sd\switch\dishonored) into Ryujinx's SD card folder, without copying the big files:
#   copied     dishonored.nro, dishonored.toml, dishonored.build.json (small; refreshed when they differ)
#   hard links game_root.nxpack/.nxidx and the deko3d shader files (same volume; relinked when the source changed)
#   junctions  update\ and dlc\ (read-only for the game)
# The game's own files (dishonored.log, userdata\, cache\) stay in Ryujinx's folder, so out\sd stays what goes on the
# real SD card. Ryujinx's data folder: $env:APPDATA\Ryujinx (or -RyujinxData).
# -Nro <file>: that NRO instead of out\sd's (a diagnostic build kept out of the SD card's folder, such as the debug
# deko3d one in out\sd_dkdebug: run_dishonored_ryujinx_renderdoc.bat dkdebug).
param(
    [string]$RyujinxData = (Join-Path $env:APPDATA 'Ryujinx'),
    [string]$Nro = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$src = Join-Path $root 'out\sd\switch\dishonored'
$dst = Join-Path $RyujinxData 'sdcard\switch\dishonored'
if (-not (Test-Path (Join-Path $src 'dishonored.nro'))) { throw "No Switch build in $src (deploy it first)" }
New-Item -ItemType Directory -Force $dst | Out-Null

function Same([System.IO.FileInfo]$a, [string]$bPath) {
    if (-not (Test-Path $bPath)) { return $false }
    $b = Get-Item $bPath
    return $a.Length -eq $b.Length -and $a.LastWriteTimeUtc -eq $b.LastWriteTimeUtc
}

foreach ($name in 'dishonored.nro', 'dishonored.build.json') {
    $f = Join-Path $src $name
    if ($Nro -and $name -eq 'dishonored.nro') { $f = $Nro }
    if ($Nro -and $name -eq 'dishonored.build.json') { $f = Join-Path (Split-Path $Nro) $name }
    if (-not (Test-Path $f)) { continue }
    $t = Join-Path $dst $name
    if (-not (Same (Get-Item $f) $t)) { Copy-Item $f $t -Force; "copied    $name" }
}

# The settings: the console's dishonored.toml plus what only Ryujinx needs (as ME2's Ryujinx me2.toml):
#   switch_guest_views_eager  map the guest memory views up front. On the console they are filled in on access (data
#                             aborts), which Ryujinx does not pass to homebrew: without it the NRO dies at startup
#                             in BaseHeap::AllocRange ("Invalid memory access at virtual address 0x...").
$ryujinxOnly = @'

# ---- Ryujinx only (added by tools/win/sync_ryujinx_dishonored.ps1; not in the SD card's copy) ----
switch_guest_views_eager = true
'@
$toml = (Get-Content -Raw -Encoding UTF8 (Join-Path $src 'dishonored.toml')).TrimEnd() + "`n" + $ryujinxOnly
$tomlPath = Join-Path $dst 'dishonored.toml'
$old = if (Test-Path $tomlPath) { Get-Content -Raw -Encoding UTF8 $tomlPath } else { '' }
if ($old -ne $toml) {
    [IO.File]::WriteAllText($tomlPath, $toml, (New-Object System.Text.UTF8Encoding($false)))
    "written   dishonored.toml (+ Ryujinx settings)"
}

foreach ($name in 'game_root.nxpack', 'game_root.nxidx', 'deko3d_shaders.bin', 'deko3d_shaders.bin.idx',
                  'deko3d_glsl.bin') {
    $f = Join-Path $src $name
    if (-not (Test-Path $f)) { "missing   $name (not in the build)"; continue }
    $t = Join-Path $dst $name
    if (Same (Get-Item $f) $t) { continue }
    if (Test-Path $t) { Remove-Item $t -Force }
    New-Item -ItemType HardLink -Path $t -Target $f | Out-Null
    "linked    $name"
}

foreach ($name in 'update', 'dlc') {
    $f = Join-Path $src $name
    $t = Join-Path $dst $name
    if (-not (Test-Path $f)) { "missing   $name\ (not in the build)"; continue }
    if (Test-Path $t) {
        $item = Get-Item $t -Force
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            "kept      $name\ (a real folder in Ryujinx's SD card, not linked)"
        }
        continue
    }
    New-Item -ItemType Junction -Path $t -Target $f | Out-Null
    "junction  $name\ -> $f"
}
"Ryujinx SD card: $dst"
