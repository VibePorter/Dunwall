@echo off
rem Dishonored (Xbox 360) on the PC build of the port: a normal window with sound.
rem Your game files come from assets\game_root and assets\update; saves and settings go to out\pc\userdata.
rem Log: out\pc\dishonored.log. Extra options can be added after the command, e.g. --fullscreen=true
setlocal
set ROOT=%~dp0
set EXE=%ROOT%out\pc\sa2.exe
rem 2026-11-09: "360" as the first option renders the scene at 360p as the Switch does (the HUD stays 720p):
rem   run_dishonored_pc.bat 360 [more options]
set HEIGHT=720
set REST=%*
if /i "%~1"=="360" (set HEIGHT=360& set "REST=%REST:~4%")
if not exist "%EXE%" (
  echo %EXE% not found: build it first ^(docs/building.md, "The PC build"^).
  pause
  exit /b 1
)
if not exist "%ROOT%out\pc\userdata" mkdir "%ROOT%out\pc\userdata"
cd /d "%ROOT%out\pc"
"%EXE%" --fullscreen=false --sa2_renderer=renut --renut_gpu_backend=vulkan ^
  --mnk_mode=true --input_xbox_layout=true --license_mask=0xFFFFFFFF ^
  --sa2_render_height_handheld=%HEIGHT% --sa2_render_height_docked=%HEIGHT% ^
  --log_file="%ROOT%out\pc\dishonored.log" --log_level=info ^
  --user_data_root="%ROOT%out\pc\userdata" --game_data_root="%ROOT%assets\game_root" ^
  --update_data_root="%ROOT%assets\update" %REST%
