@echo off
rem Dishonored's Switch build (deko3d) in Ryujinx under RenderDoc: press F12 in Ryujinx's window to save a frame
rem capture to out\rdc\ryu\dh_*.rdc. Ryujinx's own interface is drawn in software (--software-gui) so RenderDoc
rem hooks only the emulated GPU's Vulkan; the update check is off. Ryujinx's config is not changed.
rem
rem Before starting, tools\win\sync_ryujinx_dishonored.ps1 puts out\sd\switch\dishonored into Ryujinx's SD card
rem (%APPDATA%\Ryujinx\sdcard\switch\dishonored: NRO and toml copied, big files hard-linked, update\ and dlc\ linked).
rem The game's log: %APPDATA%\Ryujinx\sdcard\switch\dishonored\dishonored.log; Ryujinx's: %RYUJINX_DIR%\Logs.
rem
rem Arguments, in any order:
rem   soft     guest memory through the software page table (slower, without the host-mapped memory tricks)
rem   nordc    without RenderDoc
rem   dkdebug  the NRO of out\sd_dkdebug\switch\dishonored (debug deko3d library: its errors are written to deko3d.log)
rem   classic  the NRO of out\sd_classic\switch\dishonored (classic codegen, for an A/B against the optimised one)
rem Ryujinx starts an NRO with the stock homebrew core mask (cores 0-2): the core 3 path (NSP forwarder) is not used.
rem Close the PC build of the game first (they share the CPU-visible video memory).
setlocal
set ROOT=%~dp0
rem Ryujinx's folder: set RYUJINX_DIR, or keep Ryujinx next to the repository folder.
if not defined RYUJINX_DIR set RYUJINX_DIR=%ROOT%..\Ryujinx
set RYU=%RYUJINX_DIR%
set RDC="C:\Program Files\RenderDoc\renderdoccmd.exe"
set EXTRA=
set USE_RDC=1
set SYNCARGS=
:args
if "%~1"=="" goto run
if /i "%~1"=="soft" set EXTRA=--memory-manager-mode SoftwarePageTable
if /i "%~1"=="nordc" set USE_RDC=0
if /i "%~1"=="dkdebug" set SYNCARGS=-Nro "%ROOT%out\sd_dkdebug\switch\dishonored\dishonored.nro"
if /i "%~1"=="classic" set SYNCARGS=-Nro "%ROOT%out\sd_classic\switch\dishonored\dishonored.nro"
shift
goto args
:run
if not exist "%RYU%\Ryujinx.exe" (
  echo Ryujinx not found at %RYU%
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\win\sync_ryujinx_dishonored.ps1" %SYNCARGS%
if errorlevel 1 (
  pause
  exit /b 1
)
set NROPATH=%APPDATA%\Ryujinx\sdcard\switch\dishonored\dishonored.nro
if "%USE_RDC%"=="0" (
  echo Starting %NROPATH%
  "%RYU%\Ryujinx.exe" --hide-updates --graphics-backend Vulkan %EXTRA% "%NROPATH%"
  exit /b
)
if not exist %RDC% (
  echo RenderDoc not found at %RDC% ^(run with "nordc" to start without it^)
  pause
  exit /b 1
)
if not exist "%ROOT%out\rdc\ryu" mkdir "%ROOT%out\rdc\ryu"
echo Starting %NROPATH% under RenderDoc (F12 = capture to %ROOT%out\rdc\ryu)
%RDC% capture -d "%RYU%" -c "%ROOT%out\rdc\ryu\dh" "%RYU%\Ryujinx.exe" ^
  --software-gui --hide-updates --graphics-backend Vulkan %EXTRA% "%NROPATH%"
