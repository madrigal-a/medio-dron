@echo off
chcp 65001 >nul
cd /d "%~dp0app"
where py >nul 2>nul && (set PY=py) || (set PY=python)
%PY% -c "import serial, matplotlib" 2>nul || (
  echo Instalando pyserial y matplotlib...
  %PY% -m pip install --user pyserial matplotlib
)
%PY% banco_empuje_app.py
if errorlevel 1 pause
