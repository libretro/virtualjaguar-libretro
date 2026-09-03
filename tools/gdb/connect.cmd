@echo off
rem tools\gdb\connect.cmd -- Windows companion to connect.sh.
rem
rem   https://github.com/libretro/virtualjaguar-libretro
rem
rem See connect.sh for the full comment block (host/port defaults, why
rem this project ships no debugger, the GPU/DSP-vs-68K m68k-support
rem caveat). This is the cheap Windows equivalent: same defaults, same
rem jaguar.gdbinit / jaguar_gdb.py, no auto-detection beyond PATH.
rem
rem Usage:
rem   tools\gdb\connect.cmd
rem   tools\gdb\connect.cmd --port 3333
rem   tools\gdb\connect.cmd --gdb C:\path\to\gdb.exe
rem   tools\gdb\connect.cmd --no-python

setlocal enabledelayedexpansion

set "HOST=127.0.0.1"
set "PORT=2345"
set "USE_PYTHON=1"
if defined GDB (set "GDBBIN=%GDB%") else (set "GDBBIN=gdb")

:parseargs
if "%~1"=="" goto :afterargs
if /I "%~1"=="--host" (
  set "HOST=%~2"
  shift
  shift
  goto :parseargs
)
if /I "%~1"=="--port" (
  set "PORT=%~2"
  shift
  shift
  goto :parseargs
)
if /I "%~1"=="--gdb" (
  set "GDBBIN=%~2"
  shift
  shift
  goto :parseargs
)
if /I "%~1"=="--no-python" (
  set "USE_PYTHON=0"
  shift
  goto :parseargs
)
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage
echo Unknown option: %~1 1>&2
goto :usage

:afterargs
where "%GDBBIN%" >nul 2>nul
if errorlevel 1 (
  echo ERROR: no gdb found ^(tried "%GDBBIN%"^). 1>&2
  echo   This project's toolchain ships no debugger -- bring your own. 1>&2
  echo   See docs\gdb-stub-guide.md, "No shipped m68k-elf-gdb". 1>&2
  echo   Or: connect.cmd --gdb C:\path\to\gdb.exe 1>&2
  exit /b 1
)

set "SCRIPTDIR=%~dp0"
set "GDBINIT=%SCRIPTDIR%jaguar.gdbinit"
set "PYPLUGIN=%SCRIPTDIR%jaguar_gdb.py"

echo Connecting: %GDBBIN% -^> %HOST%:%PORT%
echo (a halt on the other end freezes the whole frontend -- see docs\gdb-stub-guide.md)

if "%USE_PYTHON%"=="1" if exist "%PYPLUGIN%" (
  "%GDBBIN%" -q -x "%GDBINIT%" -x "%PYPLUGIN%" -ex "jconnect %HOST% %PORT%"
) else (
  "%GDBBIN%" -q -x "%GDBINIT%" -ex "jconnect %HOST% %PORT%"
)
exit /b %ERRORLEVEL%

:usage
echo Usage: connect.cmd [--host HOST] [--port PORT] [--gdb PATH] [--no-python]
echo   Defaults: 127.0.0.1:2345 (matches the core's virtualjaguar_gdb_port default)
exit /b 0
