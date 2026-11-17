@echo off
rem AeroBox: programos ikelimas per USB. Nustatymai (Wi-Fi, vieta) islieka.
cd /d "%~dp0"
echo ==============================================
echo   AeroBox - programos ikelimas per USB
echo ==============================================
call "%~dp0_esptool.bat" || exit /b 1

echo.
echo Keliama i %PORT% (atskiri failai - nustatymu sritis 0x9000 nelieciama) ...
%ESPTOOL% --chip esp32s3 --port %PORT% --baud 460800 --before default_reset --after hard_reset write_flash 0x0 bin\bootloader.bin 0x8000 bin\partitions.bin 0xf000 bin\ota_data_initial.bin 0x20000 bin\firmware.bin
if errorlevel 1 goto fail

echo.
echo [OK] Ikelta. Jei ekranas neuzsidega per 5 s - paspauskite plokstes RST mygtuka.
pause
exit /b 0

:fail
echo.
echo [KLAIDA] Nepavyko. Laikykite BOOT mygtuka, trumpai paspauskite RST, atleiskite BOOT ir bandykite dar karta.
pause
exit /b 1
