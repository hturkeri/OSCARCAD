@echo off
rem PersistCAD teknik resim surumu: istege bagli kutuphaneleri kur, derle, dist_drw klasorune paketle, test et
chcp 65001 >nul
title PersistCAD teknik resim: kurulum + derle + paketle + test
set "PC_DIST=dist_drw"
call "%~dp0ortak.bat"
"%PC_BASH%" -l "%~dp0drw_paketler.sh" > "%PC_LOGS%\drw_paketler.log" 2>&1
if exist "%~dp0..\..\build-clang64\CMakeCache.txt" del /q "%~dp0..\..\build-clang64\CMakeCache.txt"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_drw\persistcad.exe" "%~dp0..\..\dist_drw\PersistCAD_DRW.exe" >nul
call "%~dp03_test.bat"
:end
echo.
echo Bitti. Loglar: %~dp0..\..\logs
