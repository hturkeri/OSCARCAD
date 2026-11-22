@echo off
rem PersistCAD yuz isimlendirme surumu: derle, dist_ns klasorune paketle, test et
chcp 65001 >nul
title PersistCAD yuz isimlendirme: derle + paketle + test
set "PC_DIST=dist_ns"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_ns\persistcad.exe" "%~dp0..\..\dist_ns\PersistCAD_NS.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
