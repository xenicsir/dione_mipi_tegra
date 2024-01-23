#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

apply_patch() {
    if [ -d "$1" ]; then
        shopt -s nullglob
        for patch_file in "$1/"*.patch; do
            patch -p 1 -i "$patch_file" -d "$2"
        done
        shopt -u nullglob
    fi
}

if [ -z "$CUSTOM_KERNEL_PATH" ]; then
    apply_patch "$KERNEL_PATCHES/kernel" "$TEGRA_KERNEL_HOME"
    apply_patch "$KERNEL_PATCHES/kernel_$L4T_VER" "$TEGRA_KERNEL_HOME"
fi

apply_patch "$KERNEL_PATCHES/nvidia_$L4T_VER" "$TEGRA_KERNEL_NV_HOME"
