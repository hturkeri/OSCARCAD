@echo off
rem PersistCAD - 1) MSYS2 + UCRT64 araç zinciri kurulumu (yönetici yetkisi gerekmez)
chcp 65001 >nul
setlocal
call "%~dp0ortak.bat"
set "LOG=%PC_LOGS%\1_kurulum.log"
del /q "%PC_LOGS%\1_kurulum.OK" "%PC_LOGS%\1_kurulum.FAIL" 2>nul
echo [%date% %time%] Kurulum basliyor (MSYS2: %PC_MSYS%) > "%LOG%"
if exist "%PC_BASH%" goto :update
echo MSYS2 indiriliyor... (yaklasik 100 MB)
echo MSYS2 indiriliyor >> "%LOG%"
curl -L --fail --retry 3 -o "%TEMP%\msys2-base.sfx.exe" https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe >> "%LOG%" 2>&1
if errorlevel 1 goto :fail
for %%I in ("%PC_MSYS%\..") do set "PC_MSYS_PARENT=%%~fI"
echo MSYS2 aciliyor: %PC_MSYS_PARENT% >> "%LOG%"
"%TEMP%\msys2-base.sfx.exe" -y -o"%PC_MSYS_PARENT%" >> "%LOG%" 2>&1
if errorlevel 1 goto :fail
echo Ilk calistirma (anahtarlar)... >> "%LOG%"
"%PC_BASH%" -lc "echo MSYS2 hazir" >> "%LOG%" 2>&1
:update
cd /d "%PC_ROOT%"
echo Paketler guncelleniyor ve kuruluyor (birkac dakika - 1.5 GB'a kadar indirme)...
"%PC_BASH%" -lc "pacman -Syuu --noconfirm" >> "%LOG%" 2>&1
"%PC_BASH%" -lc "pacman -Syuu --noconfirm" >> "%LOG%" 2>&1
"%PC_BASH%" -l "%~dp0kurulum.sh" >> "%LOG%" 2>&1
if errorlevel 1 goto :fail
echo OK > "%PC_LOGS%\1_kurulum.OK"
echo Kurulum tamam.
exit /b 0
:fail
echo FAIL > "%PC_LOGS%\1_kurulum.FAIL"
echo KURULUM HATASI - ayrinti: %LOG%
exit /b 1
