@echo off
rem SC23: hizli yeniden derle + paketle + yalniz basarisiz testleri tekrar calistir
chcp 65001 >nul
set "PC_DIST=dist_sc23"
call "%~dp02_derle.bat" || goto :end
call "%~dp04_paketle.bat" || goto :end
copy /y "%~dp0..\..\dist_sc23\persistcad.exe" "%~dp0..\..\dist_sc23\PersistCAD_SC23.exe" >nul
call "%~dp07_basarisizlari_tekrar.bat"
:end
echo Bitti.
