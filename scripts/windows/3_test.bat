@echo off
rem PersistCAD - 3) Testler (birim testleri + GUI test pencereleri kısa süre açılıp kapanır)
chcp 65001 >nul
setlocal
call "%~dp0ortak.bat"
set "LOG=%PC_LOGS%\3_test.log"
del /q "%PC_LOGS%\3_test.OK" "%PC_LOGS%\3_test.FAIL" 2>nul
cd /d "%PC_ROOT%"
echo Testler calisiyor... %LOG%
"%PC_BASH%" -lc "export PATH=$MINGW_PREFIX/bin:$PATH; ctest --test-dir build-$(echo $MSYSTEM | tr A-Z a-z) -j2 --output-on-failure --timeout 600" > "%LOG%" 2>&1
if errorlevel 1 (echo FAIL > "%PC_LOGS%\3_test.FAIL" & echo BAZI TESTLER BASARISIZ - %LOG% & exit /b 1)
echo OK > "%PC_LOGS%\3_test.OK"
echo Tum testler gecti.
