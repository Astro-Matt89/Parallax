#!/usr/bin/env bash
# Compile all GLSL shaders to SPIR-V
# Usage: ./shaders/compile_shaders.sh [output_dir]
#
# Requires glslangValidator on PATH (bundled with Vulkan SDK).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${1:-${SCRIPT_DIR}}"

echo "Shader source: ${SCRIPT_DIR}"
echo "SPIR-V output: ${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

for shader in "${SCRIPT_DIR}"/*.vert "${SCRIPT_DIR}"/*.frag "${SCRIPT_DIR}"/*.comp; do
    [ -f "${shader}" ] || continue
    name="$(basename "${shader}")"
    echo "  Compiling: ${name} -> ${name}.spv"
    glslangValidator -V "${shader}" -o "${OUTPUT_DIR}/${name}.spv"
done

echo "Done."