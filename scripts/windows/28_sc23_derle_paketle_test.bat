@echo off
rem PersistCAD SpaceClaim 2023 arayuzu: derle, dist_sc23 klasorune paketle, test et
chcp 65001 >nul
title PersistCAD SC23: derle + paketle + test
set "PC_DIST=dist_sc23"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_sc23\persistcad.exe" "%~dp0..\..\dist_sc23\PersistCAD_SC23.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
