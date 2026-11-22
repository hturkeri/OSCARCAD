@echo off
rem PersistCAD - derle + paketle + test, sonra enjektor uygulamasini ac
chcp 65001 >nul
title PersistCAD derle + paketle + test
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
call "%~dp03_test.bat"
start "" "%~dp0..\..\dist\Enjektor_09DB01_App.exe" --section xz
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
