@echo off
rem Dishonored (Xbox 360) on the PC build under RenderDoc: press F12 (or Print Screen) in the game window to save a
rem frame capture to out\rdc\dh_*.rdc (each press = one capture). Open them with qrenderdoc.exe.
rem Same settings as run_dishonored_pc.bat; log: out\pc\dishonored_rdc.log. Extra options can follow the command.
setlocal
set ROOT=%~dp0
set EXE=%ROOT%out\pc\sa2.exe
rem 2026-11-09: "360" as the first option renders the scene at 360p as the Switch does (the HUD stays 720p):
rem   run_dishonored_pc_renderdoc.bat 360 [more options]
set HEIGHT=720
set REST=%*
if /i "%~1"=="360" (set HEIGHT=360& set "REST=%REST:~4%")
set RDC="C:\Program Files\RenderDoc\renderdoccmd.exe"
if not exist "%EXE%" (
  echo %EXE% not found: build it first ^(docs/building.md, "The PC build"^).
  pause
  exit /b 1
)
if not exist %RDC% (
  echo RenderDoc not found at %RDC%
  pause
  exit /b 1
)
if not exist "%ROOT%out\pc\userdata" mkdir "%ROOT%out\pc\userdata"
if not exist "%ROOT%out\rdc" mkdir "%ROOT%out\rdc"
cd /d "%ROOT%out\pc"
rem banjo_renut_store_cards=false: as the ME2 RenderDoc script (the store cards' page marks are not needed for captures).
%RDC% capture -d "%ROOT%out\pc" -c "%ROOT%out\rdc\dh" "%EXE%" --fullscreen=false --sa2_renderer=renut ^
  --renut_gpu_backend=vulkan --mnk_mode=true --input_xbox_layout=true --license_mask=0xFFFFFFFF ^
  --sa2_render_height_handheld=%HEIGHT% --sa2_render_height_docked=%HEIGHT% --banjo_renut_store_cards=false --dh_draw_trace_rdc=true ^
  --log_file="%ROOT%out\pc\dishonored_rdc.log" --log_level=info ^
  --user_data_root="%ROOT%out\pc\userdata" --game_data_root="%ROOT%assets\game_root" ^
  --update_data_root="%ROOT%assets\update" %REST%
