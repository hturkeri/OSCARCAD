@rem Ortak ayarlar: MSYS2 konumu, proje kökü, log klasörü.
set "PC_ROOT=%~dp0..\.."
for %%I in ("%PC_ROOT%") do set "PC_ROOT=%%~fI"
set "PC_LOGS=%PC_ROOT%\logs"
if not exist "%PC_LOGS%" mkdir "%PC_LOGS%"
if "%PC_MSYS%"=="" set "PC_MSYS=%USERPROFILE%\msys64"
if "%PC_ENV%"=="" set "PC_ENV=clang64"
if /i "%PC_ENV%"=="clang64" (set "MSYSTEM=CLANG64") else (set "MSYSTEM=UCRT64")
set "CHERE_INVOKING=1"
set "PC_BASH=%PC_MSYS%\usr\bin\bash.exe"
