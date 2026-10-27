@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo Compiling Vulkan shaders to Energy Engine Shader Files (.eesf)...
echo.

where glslc >nul 2>nul
if errorlevel 1 (
    echo ERROR: glslc was not found in PATH.
    echo Install the Vulkan SDK or add glslc to PATH.
    pause
    exit /b 1
)

set "SHADER_COUNT=0"
set "ERROR_COUNT=0"

for /r %%f in (*.vert *.frag *.geom *.tesc *.tese *.comp *.mesh *.task *.rgen *.rint *.rahit *.rchit *.rmiss *.rcall *.glsl *.vs *.glvs *.fs *.glfs *.gs *.gl *.shader) do (
    echo Compiling: %%f
    set "TEMP_SPV=%%f.__energy_tmp.spv"
    set "TEMP_HEADER=%%f.__energy_header.tmp"
    set "TEMP_EESF=%%f.__energy_tmp.eesf"

    glslc "%%f" -o "!TEMP_SPV!"
    if errorlevel 1 (
        echo ERROR: Failed to compile %%f
        set /a ERROR_COUNT+=1
    ) else (
        >"!TEMP_HEADER!" echo ENERGY ENGINE SHADER FILE v1.0.0
        copy /b /y "!TEMP_HEADER!"+"!TEMP_SPV!" "!TEMP_EESF!" >nul
        if errorlevel 1 (
            echo ERROR: Failed to package %%f as .eesf
            set /a ERROR_COUNT+=1
        ) else (
            move /y "!TEMP_EESF!" "%%f.eesf" >nul
            if errorlevel 1 (
                echo ERROR: Failed to write %%f.eesf
                set /a ERROR_COUNT+=1
            ) else (
                set /a SHADER_COUNT+=1
            )
        )
    )

    if exist "!TEMP_SPV!" del /q "!TEMP_SPV!" >nul 2>nul
    if exist "!TEMP_HEADER!" del /q "!TEMP_HEADER!" >nul 2>nul
    if exist "!TEMP_EESF!" del /q "!TEMP_EESF!" >nul 2>nul
)

echo.
echo Successfully compiled: !SHADER_COUNT!
echo Failed: !ERROR_COUNT!
echo.

if not "!ERROR_COUNT!"=="0" (
    pause
    exit /b 1
)

echo All shaders compiled successfully.
pause
exit /b 0
