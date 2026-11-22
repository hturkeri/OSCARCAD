@echo off
rem PersistCAD SpaceClaim duzeni - derle, dist_sc klasorune paketle, test et
chcp 65001 >nul
title PersistCAD SC derle + paketle + test
set "PC_DIST=dist_sc"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_sc\persistcad.exe" "%~dp0..\..\dist_sc\PersistCAD_SC.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
