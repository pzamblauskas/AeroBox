@echo off
rem AeroBox: visiskas isvalymas + ikelimas. Istrina ir Wi-Fi bei visus nustatymus.
rem Naudoti pirma karta ir kai plokste "nieko nedaro" po ankstesnio ikelimo.
cd /d "%~dp0"
echo ==============================================
echo   AeroBox - VISKA ISTRINTI ir ikelti is naujo
echo ==============================================
echo Bus istrinta visa atmintis (Wi-Fi, vieta, nustatymai).
call "%~dp0_esptool.bat" || exit /b 1

echo.
echo 1/2 Trinama visa atmintis (iki 30 s) ...
%ESPTOOL% --chip esp32s3 --port %PORT% --baud 460800 --before default_reset --after no_reset erase_flash
if errorlevel 1 goto fail

echo.
echo 2/2 Keliama bin\firmware_merged.bin ...
%ESPTOOL% --chip esp32s3 --port %PORT% --baud 460800 --before default_reset --after hard_reset write_flash 0x0 bin\firmware_merged.bin
if errorlevel 1 goto fail

echo.
echo [OK] Ikelta. Ekrane turi pasirodyti AEROBOX, paskui "WI-FI SETUP".
echo Jei ekranas tuscias per 5 s - paspauskite plokstes RST mygtuka.
echo Telefonu prisijunkite prie Wi-Fi tinklo AeroBox (slaptazodis rodomas ekrane) - puslapis atsidarys pats.
pause
exit /b 0

:fail
echo.
echo [KLAIDA] Nepavyko. Laikykite BOOT mygtuka, trumpai paspauskite RST, atleiskite BOOT ir bandykite dar karta.
pause
exit /b 1
