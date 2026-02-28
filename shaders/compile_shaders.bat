@echo off
REM Compile all GLSL shaders to SPIR-V
REM Usage: shaders\compile_shaders.bat [output_dir]
REM
REM Requires glslangValidator on PATH (bundled with Vulkan SDK).

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set OUTPUT_DIR=%~1
if "%OUTPUT_DIR%"=="" set OUTPUT_DIR=%SCRIPT_DIR%

echo Shader source: %SCRIPT_DIR%
echo SPIR-V output: %OUTPUT_DIR%
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

for %%f in ("%SCRIPT_DIR%*.vert" "%SCRIPT_DIR%*.frag" "%SCRIPT_DIR%*.comp") do (
    echo   Compiling: %%~nxf -^> %%~nxf.spv
    glslangValidator -V "%%f" -o "%OUTPUT_DIR%\%%~nxf.spv"
    if errorlevel 1 (
        echo ERROR: Failed to compile %%~nxf
        exit /b 1
    )
)

echo Done.