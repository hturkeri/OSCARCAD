@echo off
chcp 65001 >nul
setlocal
call "%~dp0ortak.bat"
cd /d "%PC_ROOT%"
echo Teshis calisiyor...
"%PC_BASH%" -l "%~dp0teshis.sh"
echo Bitti.
