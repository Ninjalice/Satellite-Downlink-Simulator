@echo off
setlocal ENABLEEXTENSIONS

echo ======================================================
echo   Satellite Downlink Simulator - Package Builder
echo ======================================================
echo.

set "ROOT=%~dp0"
pushd "%ROOT%" >nul

set "BUILD_DIR=build"
set "CONFIG=Release"
set "EXE=%BUILD_DIR%\%CONFIG%\simulator.exe"
set "PKG_DIR=release_package"
set "ZIP_NAME=SatelliteDownlinkSimulator.zip"

REM 1) Ensure build exists
if not exist "%EXE%" (
    echo [1/5] simulator.exe not found. Building Release...
    if not exist "%BUILD_DIR%" (
        cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
        if errorlevel 1 (
            echo ERROR: CMake configure failed.
            popd >nul
            pause
            exit /b 1
        )
    )
    cmake --build "%BUILD_DIR%" --config %CONFIG% --target simulator
    if errorlevel 1 (
        echo ERROR: Build failed.
        popd >nul
        pause
        exit /b 1
    )
) else (
    echo [1/5] Found existing build: %EXE%
)

echo [2/5] Preparing package folder...
if exist "%PKG_DIR%" rmdir /s /q "%PKG_DIR%"
mkdir "%PKG_DIR%"
if errorlevel 1 (
    echo ERROR: Could not create package folder.
    popd >nul
    pause
    exit /b 1
)

echo [3/5] Copying binary and config...
copy /y "%EXE%" "%PKG_DIR%\" >nul
if errorlevel 1 (
    echo ERROR: Could not copy simulator.exe
    popd >nul
    pause
    exit /b 1
)

if not exist "config" (
    echo ERROR: Missing config folder.
    popd >nul
    pause
    exit /b 1
)
xcopy "config" "%PKG_DIR%\config\" /E /I /Y >nul
if errorlevel 1 (
    echo ERROR: Could not copy config folder.
    popd >nul
    pause
    exit /b 1
)

if exist "README.md" copy /y "README.md" "%PKG_DIR%\" >nul
if exist "earth_texture.jpg" copy /y "earth_texture.jpg" "%PKG_DIR%\" >nul
if exist "earth_texture.png" copy /y "earth_texture.png" "%PKG_DIR%\" >nul

echo [4/5] Creating zip...
if exist "%ZIP_NAME%" del /f /q "%ZIP_NAME%" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%PKG_DIR%\*' -DestinationPath '%ZIP_NAME%' -Force"
if errorlevel 1 (
    echo ERROR: Could not create zip file.
    popd >nul
    pause
    exit /b 1
)

echo [5/5] Done.
echo.
echo Package folder: %PKG_DIR%
echo Zip file: %ZIP_NAME%
echo.
echo You can now send %ZIP_NAME%.
echo.
popd >nul
pause
exit /b 0
