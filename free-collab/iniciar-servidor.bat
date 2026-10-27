@echo off
title Servidor Free Collab
cd /d "%~dp0server"
where node >nul 2>nul
if errorlevel 1 (
  echo No tienes Node.js. Instalalo desde https://nodejs.org y vuelve a abrir este archivo.
  pause
  exit /b
)
echo Dejando el servidor abierto. No cierres esta ventana mientras colaboran.
node server.js
pause
