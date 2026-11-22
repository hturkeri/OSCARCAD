@echo off
rem PersistCAD gizle/goster + cek duzeltmesi: derle, dist_vis klasorune paketle, test et
chcp 65001 >nul
title PersistCAD gizle/goster: derle + paketle + test
set "PC_DIST=dist_vis"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_vis\persistcad.exe" "%~dp0..\..\dist_vis\PersistCAD_VIS.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
