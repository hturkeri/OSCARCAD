@echo off
rem PersistCAD gercek cizim surumu: derle, dist_real klasorune paketle, test et
chcp 65001 >nul
title PersistCAD gercek cizim: derle + paketle + test
set "PC_DIST=dist_real"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_real\persistcad.exe" "%~dp0..\..\dist_real\PersistCAD_REAL.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
