#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

rm -Rf "$TEGRA_KERNEL_OUT"
rm -Rf "$TEGRA_HOME"

# extract driver package
tar xf "$DL_DIR/$DRV_FILE" -C "$BUILD_HOME"

# extract kernel source
tar xf "$DL_DIR/$SRC_FILE" -C "$BUILD_HOME"

# extract kernel source
tar xf "$TEGRA_HOME/source/public/kernel_src.tbz2" -C "$TEGRA_HOME/source/public"

# extract sample rootfs
if [ -n "$ROOTFS_FILE" ]; then
    rm -Rf "$ROOTFS_HOME"
    mkdir -p "$ROOTFS_HOME"

    tar xf "$DL_DIR/$ROOTFS_FILE" -C "$ROOTFS_HOME"
fi
