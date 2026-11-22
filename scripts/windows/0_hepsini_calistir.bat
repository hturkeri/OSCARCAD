@echo off
rem PersistCAD - Windows kurulum + derleme + test + paketleme (sırayla)
chcp 65001 >nul
title PersistCAD Windows derleme
call "%~dp01_kurulum.bat" || goto :end
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
pause
