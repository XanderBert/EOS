@echo off

REM Define the build directory name
set BUILD_DIR=build

REM Create the build directory if it doesn't exist
if not exist "%BUILD_DIR%" (
    echo --- Creating build directory: %BUILD_DIR% ---
    mkdir "%BUILD_DIR%"
)

REM Navigate into the build directory
cd "%BUILD_DIR%"

REM Configure the project using CMake and Ninja generator
echo --- Configuring project with CMake (using Ninja) ---
cmake -G Ninja ..
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    exit /b %errorlevel%
)


REM Build the project using Ninja
echo --- Building project with Ninja ---
ninja
if %errorlevel% neq 0 (
    echo Ninja build failed!
    exit /b %errorlevel%
)

echo --- Build finished successfully! ---

cd ..
