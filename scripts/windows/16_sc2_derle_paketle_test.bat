@echo off
rem PersistCAD - derle, dist_sc2 klasorune paketle, test et
chcp 65001 >nul
title PersistCAD derle + paketle + test (dist_sc2)
set "PC_DIST=dist_sc2"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_sc2\persistcad.exe" "%~dp0..\..\dist_sc2\PersistCAD_SC2.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
