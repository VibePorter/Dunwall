# Dishonored (2026-11-08): one RenderDoc capture of the PC build, without a visible window or a key press.
# Starts out\pc\sa2.exe through renderdoccmd capture with the smoke-run settings and dh_renderdoc_capture_frame = -Frame
# (app/dishonored_src/dh_hooks.cpp starts the capture at that game frame and ends it two frames later), waits for the
# .rdc, then stops the game. Output: out\rdc\<Name>_*.rdc and out\rdc\<Name>.log.
#   powershell -File tools\win\rdc_capture_dishonored.ps1 [-Name menu] [-Frame 2400] [-TimeoutSeconds 180]
param([string]$Name = 'menu', [int]$Frame = 2400, [int]$TimeoutSeconds = 180, [string[]]$ExtraArguments = @())
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exeFolder = Join-Path $root 'out\pc'
$exe = Join-Path $exeFolder 'sa2.exe'
$rdcDir = Join-Path $root 'out\rdc'
$userdata = Join-Path $rdcDir 'userdata'
New-Item -ItemType Directory -Force $rdcDir, $userdata | Out-Null
$log = Join-Path $rdcDir "$Name.log"
$started = Get-Date
$gameArgs = @('--fullscreen=false', '--window_start_minimized=true', '--audio_mute=true', '--sa2_renderer=renut',
    '--renut_gpu_backend=vulkan', '--mnk_mode=true', '--input_xbox_layout=true', '--license_mask=0xFFFFFFFF',
    '--sa2_render_height_handheld=720', '--sa2_render_height_docked=720', '--banjo_deko3d_alpha_spec=false',
    '--banjo_renut_store_cards=false', "--dh_renderdoc_capture_frame=$Frame",
    "--log_file=`"$log`"", '--log_level=info', '--log_flush_interval=1',
    "--user_data_root=`"$userdata`"", "--game_data_root=`"$root\assets\game_root`"",
    "--update_data_root=`"$root\assets\update`"") + $ExtraArguments
$rdcArgs = @('capture', '-d', "`"$exeFolder`"", '-c', "`"$rdcDir\$Name`"", "`"$exe`"") + $gameArgs
$p = Start-Process 'C:\Program Files\RenderDoc\renderdoccmd.exe' -ArgumentList $rdcArgs -WorkingDirectory $exeFolder `
    -WindowStyle Hidden -PassThru
$deadline = $started.AddSeconds($TimeoutSeconds)
$capture = $null
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    $capture = Get-ChildItem $rdcDir -Filter "$Name*.rdc" -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -gt $started } | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($capture -and (Test-Path $log) -and (Select-String -Path $log -Pattern 'RenderDoc capture ended' -Quiet)) {
        Start-Sleep -Seconds 5  # let RenderDoc finish writing
        break
    }
}
Get-Process sa2 -ErrorAction SilentlyContinue | Stop-Process -Force
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
if (Test-Path $log) { Select-String -Path $log -Pattern '\[dh-rdc\]' | ForEach-Object { $_.Line } }
$capture = Get-ChildItem $rdcDir -Filter "$Name*.rdc" -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $started } | Sort-Object LastWriteTime | Select-Object -Last 1
if ($capture) { "capture: $($capture.FullName) ($([int]($capture.Length / 1MB)) MB)" } else { 'no capture written' }
