@echo off
setlocal

echo === Building Liquidity Arena (Windows) ===

if not exist build mkdir build
cd build

echo Configuring CMake...
cmake .. -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo CMake configuration failed.
    exit /b %errorlevel%
)

echo Compiling...
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
if %errorlevel% neq 0 (
    echo Build failed.
    exit /b %errorlevel%
)

echo Build complete! Executables are in build\Release
exit /b 0
