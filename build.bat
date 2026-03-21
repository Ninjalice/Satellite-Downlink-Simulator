@echo off
echo ======================================================
echo   Simulador Orbital - Compilacion automatica
echo ======================================================
echo.

REM Verificar CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake no encontrado. Instala CMake desde https://cmake.org/download/
    pause
    exit /b 1
)

REM Verificar Git (necesario para FetchContent)
where git >nul 2>&1
if errorlevel 1 (
    echo ERROR: Git no encontrado. Instala Git desde https://git-scm.com/
    pause
    exit /b 1
)

echo [1/3] Configurando proyecto con CMake...

REM Limpiar build anterior para evitar conflictos de generador
if exist build (
    echo Limpiando build anterior...
    rmdir /s /q build
)
mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo ERROR: No se pudo configurar CMake. Verifica tu compilador.
    pause
    exit /b 1
)

echo.
echo [2/3] Compilando (la primera vez tarda porque descarga dependencias)...
cmake --build . --config Release
if errorlevel 1 (
    echo ERROR: Fallo la compilacion.
    pause
    exit /b 1
)

echo.
echo [3/3] Listo!
echo.
echo Ejecuta: build\Release\simulator.exe
echo.
pause
