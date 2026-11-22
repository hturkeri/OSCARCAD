@echo off
rem PersistCAD - 2) Derleme (MinGW UCRT64, Release)
chcp 65001 >nul
setlocal
call "%~dp0ortak.bat"
set "LOG=%PC_LOGS%\2_derle.log"
del /q "%PC_LOGS%\2_derle.OK" "%PC_LOGS%\2_derle.FAIL" 2>nul
cd /d "%PC_ROOT%"
echo Derleniyor... (ilk derleme 10-30 dk surebilir) ilerleme: %LOG%
"%PC_BASH%" -l "%~dp0derle.sh" > "%LOG%" 2>&1
if errorlevel 1 (echo FAIL > "%PC_LOGS%\2_derle.FAIL" & echo DERLEME HATASI - %LOG% & exit /b 1)
echo OK > "%PC_LOGS%\2_derle.OK"
echo Derleme tamam.
