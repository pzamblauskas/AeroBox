@echo off
rem Pagalbinis: suranda esptool (per uv arba Python) ir paklausia COM prievado.
rem Kvieciamas is kitu .bat failu, savarankiskai nenaudoti.

where uvx >nul 2>nul
if %errorlevel%==0 (
    set "ESPTOOL=uvx --from esptool==4.8.1 esptool"
    goto port
)
python -c "import esptool" >nul 2>nul
if %errorlevel%==0 (
    set "ESPTOOL=python -m esptool"
    goto port
)
echo.
echo [KLAIDA] Nerastas esptool.
echo Idiekite viena is ju:
echo   1) uv:      powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
echo   2) Python:  https://www.python.org  ir tada:  pip install esptool
echo Arba naudokite narsykles buda (README.md, B budas) - nieko diegti nereikia.
echo.
pause
exit /b 1

:port
echo.
echo Rasti COM prievadai:
powershell -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object { '  ' + $_ }"
echo.
echo ESP32-S3 jungiamas per USB jungti, pazymeta "USB" (ne "UART"/"COM"), jei plokste turi dvi.
set "PORT="
set /p PORT=Iveskite COM prievada (pvz. COM8):
if "%PORT%"=="" (
    echo Prievadas neivestas.
    pause
    exit /b 1
)
exit /b 0
