@echo off
setlocal
call "%~dp0ortak.bat"
cd /d "%PC_ROOT%"
"%PC_BASH%" -l "%~dp0occt_test.sh"
