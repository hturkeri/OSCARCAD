@echo off
rem PersistCAD - 4) Dağıtım klasörü (dist): exe + DLL + Qt eklentileri + örnek uygulamalar
chcp 65001 >nul
setlocal
call "%~dp0ortak.bat"
set "LOG=%PC_LOGS%\4_paketle.log"
del /q "%PC_LOGS%\4_paketle.OK" "%PC_LOGS%\4_paketle.FAIL" 2>nul
cd /d "%PC_ROOT%"
"%PC_BASH%" -l "%~dp0paketle.sh" > "%LOG%" 2>&1
if errorlevel 1 (echo FAIL > "%PC_LOGS%\4_paketle.FAIL" & echo PAKETLEME HATASI - %LOG% & exit /b 1)
echo OK > "%PC_LOGS%\4_paketle.OK"
echo dist klasoru hazir
