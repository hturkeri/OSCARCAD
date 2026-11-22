@echo off
rem PersistCAD 2B-3B (SpaceClaim yontemi): derle, dist_v2d klasorune paketle, test et
chcp 65001 >nul
title PersistCAD 2B-3B: derle + paketle + test
set "PC_DIST=dist_v2d"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_v2d\persistcad.exe" "%~dp0..\..\dist_v2d\PersistCAD_V2D.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
