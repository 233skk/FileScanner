@echo off
cd /d %~dp0
if exist build rmdir /s /q build
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
echo.
echo ==============================
echo   Build done!
echo   DLL:  build\libFileScanner.dll
echo   Test: build\FileScannerTest.exe
echo ==============================
