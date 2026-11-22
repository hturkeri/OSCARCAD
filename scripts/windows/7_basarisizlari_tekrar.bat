@echo off
rem PersistCAD - basarisiz testleri tek tek (paralel olmadan) yeniden calistirir
chcp 65001 >nul
setlocal
call "%~dp0ortak.bat"
cd /d "%PC_ROOT%"
"%PC_BASH%" -lc "export PATH=$MINGW_PREFIX/bin:$PATH; ctest --test-dir build-$(echo $MSYSTEM | tr A-Z a-z) --rerun-failed -j1 --output-on-failure" > "%PC_LOGS%\7_tekrar.log" 2>&1
echo Bitti: %PC_LOGS%\7_tekrar.log
