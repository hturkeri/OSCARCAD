@echo off
rem PersistCAD - degisikliklerden sonra: derle + test
chcp 65001 >nul
title PersistCAD derle + test
call "%~dp02_derle.bat" || goto :end
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
