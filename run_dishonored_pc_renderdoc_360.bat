@echo off
rem Dishonored on the PC build under RenderDoc with the scene rendered at 360p, as the Switch does in handheld mode
rem (the HUD stays 720p). Same as "run_dishonored_pc_renderdoc.bat 360": press F12 (or Print Screen) in the game
rem window to save a frame capture to out\rdc\dh_*.rdc. Log: out\pc\dishonored_rdc.log. Extra options can follow.
call "%~dp0run_dishonored_pc_renderdoc.bat" 360 %*
