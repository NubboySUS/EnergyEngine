#!/usr/bin/env bash

set -u

printf 'Compiling Vulkan shaders to Energy Engine Shader Files (.eesf)...\n\n'

if ! command -v glslc >/dev/null 2>&1; then
    echo "ERROR: glslc was not found in PATH."
    echo "Install the Vulkan SDK or add glslc to PATH."
    exit 1
fi

SHADER_COUNT=0
ERROR_COUNT=0

while IFS= read -r -d '' shader; do
    echo "Compiling: $shader"

    temp_spv="${shader}.__energy_tmp.spv"
    temp_eesf="${shader}.__energy_tmp.eesf"
    output_eesf="${shader}.eesf"

    if glslc "$shader" -o "$temp_spv"; then
        if { printf 'ENERGY ENGINE SHADER FILE v1.0.0\r\n' && cat "$temp_spv"; } > "$temp_eesf" \
            && mv -f "$temp_eesf" "$output_eesf"; then
            SHADER_COUNT=$((SHADER_COUNT + 1))
        else
            echo "ERROR: Failed to package $shader as .eesf"
            ERROR_COUNT=$((ERROR_COUNT + 1))
        fi
    else
        echo "ERROR: Failed to compile $shader"
        ERROR_COUNT=$((ERROR_COUNT + 1))
    fi

    rm -f "$temp_spv" "$temp_eesf"
done < <(
    find . -type f \( \
        -name '*.vert' -o \
        -name '*.frag' -o \
        -name '*.geom' -o \
        -name '*.tesc' -o \
        -name '*.tese' -o \
        -name '*.comp' -o \
        -name '*.mesh' -o \
        -name '*.task' -o \
        -name '*.rgen' -o \
        -name '*.rint' -o \
        -name '*.rahit' -o \
        -name '*.rchit' -o \
        -name '*.rmiss' -o \
        -name '*.rcall' -o \
        -name '*.glsl' -o \
        -name '*.vs' -o \
        -name '*.glvs' -o \
        -name '*.fs' -o \
        -name '*.glfs' -o \
        -name '*.gs' -o \
        -name '*.gl' -o \
        -name '*.shader' \
    \) -print0
)

printf '\nSuccessfully compiled: %s\nFailed: %s\n\n' "$SHADER_COUNT" "$ERROR_COUNT"

if (( ERROR_COUNT > 0 )); then
    exit 1
fi

echo "All shaders compiled successfully."
