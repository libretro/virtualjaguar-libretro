@echo off
REM Convert a Jaguar CD CUE/BIN to CHD. See jagcd-to-chd (the Unix script) for behavior.
setlocal
set "SCRIPT_DIR=%~dp0"
if "%~1"=="" (
  echo usage: %~nx0 INPUT.cue [OUTPUT.chd]
  exit /b 1
)
if defined JAGCD_CHDMAN (
  set "CHDMAN=%JAGCD_CHDMAN%"
) else if exist "%SCRIPT_DIR%chdman.exe" (
  set "CHDMAN=%SCRIPT_DIR%chdman.exe"
) else (
  set "CHDMAN=chdman"
)
set "IN=%~1"
if "%~2"=="" (
  set "OUT=%~dpn1.chd"
) else (
  set "OUT=%~2"
)
"%CHDMAN%" createcd -i "%IN%" -o "%OUT%" -f
if errorlevel 1 exit /b 1
if exist "%SCRIPT_DIR%jagcd-chd-check.exe" (
  "%SCRIPT_DIR%jagcd-chd-check.exe" "%OUT%"
  exit /b %ERRORLEVEL%
)
"%CHDMAN%" dumpmeta -i "%OUT%" -t CHSE -ix 0 >nul 2>&1
if errorlevel 1 (
  "%CHDMAN%" dumpmeta -i "%OUT%" -t CHSE -ix 1 >nul 2>&1
  if errorlevel 1 (
    echo no CHSE tag -- chdman is too old. See docs/jagcd-chd.md
    exit /b 1
  )
)
echo CHSE present
exit /b 0
