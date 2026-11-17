@echo off
rem SkyPanel: plokstes zurnalas (log) - ka ji daro paleidimo metu. Iseiti: Ctrl+C.
cd /d "%~dp0"
echo ==============================================
echo   SkyPanel - zurnalas (diagnostikai)
echo ==============================================
where uvx >nul 2>nul
if errorlevel 1 (
    echo [KLAIDA] Reikia uv: powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
    pause
    exit /b 1
)
call "%~dp0_esptool.bat" || exit /b 1
echo.
echo Paspauskite plokstes RST mygtuka, kad matytumete paleidima nuo pradziu.
echo Svarbiausios eilutes: "paleidimas: ...", "HUB75 ...", "ekranas ...", klaidos (E).
echo.
uvx --from platformio pio device monitor -p %PORT% -b 115200 -f esp32_exception_decoder
pause
