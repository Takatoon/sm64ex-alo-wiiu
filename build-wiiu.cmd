@echo off
setlocal
cd /d "%~dp0"
docker compose -f docker-compose.wiiu.yml build --quiet wiiu-dev
if errorlevel 1 exit /b %ERRORLEVEL%
docker compose -f docker-compose.wiiu.yml run --rm --no-deps wiiu-dev bash ./build-wiiu-textures.sh %*
exit /b %ERRORLEVEL%
