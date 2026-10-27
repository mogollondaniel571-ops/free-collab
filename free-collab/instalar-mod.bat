@echo off
title Instalar Free Collab
set "GD=C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash"
if not exist "%GD%\geode" set "GD=%ProgramFiles%\Steam\steamapps\common\Geometry Dash"
if not exist "%GD%\geode" (
  echo No encontre Geometry Dash en la ruta normal de Steam.
  echo Arrastra aqui la carpeta de tu Geometry Dash y pulsa Enter:
  set /p GD=
)
set "GD=%GD:"=%"
if not exist "%GD%\geode\mods" mkdir "%GD%\geode\mods"
copy /Y "%~dp0*.geode" "%GD%\geode\mods\"
if errorlevel 1 (
  echo No encontre el archivo .geode junto a este instalador.
) else (
  echo Listo. Abre el juego y entra a los ajustes de Free Collab para poner tu nombre.
)
pause
