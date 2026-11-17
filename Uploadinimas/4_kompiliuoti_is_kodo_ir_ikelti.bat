@echo off
rem SkyPanel: sukompiliuoti is ..\SkyPanel kodo, ikelti ir atnaujinti bin\ failus.
cd /d "%~dp0"
echo ==============================================
echo   SkyPanel - kompiliuoti is kodo ir ikelti
echo ==============================================
where uvx >nul 2>nul
if errorlevel 1 (
    echo [KLAIDA] Reikia uv: powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
    pause
    exit /b 1
)
call "%~dp0_esptool.bat" || exit /b 1

pushd "%~dp0..\SkyPanel"
uvx --from platformio pio run -e esp32-s3-n16r8 -t upload --upload-port %PORT%
if errorlevel 1 (
    popd
    echo [KLAIDA] Kompiliavimas arba ikelimas nepavyko.
    pause
    exit /b 1
)
popd

set "B=%~dp0..\SkyPanel\.pio\build\esp32-s3-n16r8"
copy /y "%B%\bootloader.bin" bin\ >nul
copy /y "%B%\partitions.bin" bin\ >nul
copy /y "%B%\ota_data_initial.bin" bin\ >nul
copy /y "%B%\firmware.bin" bin\ >nul
%ESPTOOL% --chip esp32s3 merge_bin -o bin\firmware_merged.bin --flash_mode keep --flash_size 16MB 0x0 bin\bootloader.bin 0x8000 bin\partitions.bin 0xf000 bin\ota_data_initial.bin 0x20000 bin\firmware.bin
echo.
echo [OK] Ikelta, bin\ failai atnaujinti.
pause
