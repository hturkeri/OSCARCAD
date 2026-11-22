@echo off
rem PersistCAD v6 - derle, dist_v6 klasorune paketle, test et (acik olan eski surume dokunmaz)
chcp 65001 >nul
title PersistCAD v6 derle + paketle + test
set "PC_DIST=dist_v6"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
