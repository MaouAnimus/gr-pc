@echo off
cd /d "%~dp0"
title Gravity Rush - Runtime Inspector
echo ========================================================
echo  Gravity Rush - Experimental Runtime Inspector
echo ========================================================
echo.

set PATH=C:\msys64\clang64\bin;%PATH%

if exist "out\release\launcher.exe" (
    echo [+] Starting executable inspection (Release)...
    out\release\launcher.exe %*
) else if exist "out\debug\launcher.exe" (
    echo [!] Release binary not found, starting Debug...
    out\debug\launcher.exe %*
) else (
    echo [-] Error: Launcher executable not found.
    echo Run: powershell -ExecutionPolicy Bypass -File tools\build_runtime.ps1
    pause
    exit /b 1
)

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [!] Launcher exited with code %ERRORLEVEL%.
    pause
)
