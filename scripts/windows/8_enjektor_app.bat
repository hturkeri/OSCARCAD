@echo off
rem PersistCAD - degisiklikleri derle, dist'i yeniden paketle (enjektor app dahil)
chcp 65001 >nul
title PersistCAD enjektor app
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
start "" "%~dp0..\..\dist\Enjektor_09DB01_App.exe"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
