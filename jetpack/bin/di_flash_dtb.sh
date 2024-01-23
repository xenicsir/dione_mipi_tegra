#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

IN_DTB_DIR="$TEGRA_KERNEL_OUT/arch/arm64/boot/dts"
IN_IMAGE="$TEGRA_KERNEL_OUT/arch/arm64/boot/Image"
OUT_DTB_DIR="$TEGRA_HOME/kernel/dtb"
OUT_IMAGE="$TEGRA_HOME/kernel/Image"

#backup the original files
[ ! -d "$OUT_DTB_DIR"_base ] && cp -p -R "$OUT_DTB_DIR" "$OUT_DTB_DIR"_base
[ ! -f "$OUT_IMAGE"_base ] && cp -p "$OUT_IMAGE" "$OUT_IMAGE"_base

cp "$IN_IMAGE" "$OUT_IMAGE"
rm "$OUT_DTB_DIR"/*.*
cp "$IN_DTB_DIR"/*.* "$OUT_DTB_DIR"

cd "$TEGRA_HOME"

#read -n 1 -s -r -p "Restart the board in recovery mode then press any key to continue"
sudo ./flash.sh -k DTB jetson-nano-2gb-devkit mmcblk0p1

# not working for jetson nano series
#read -n 1 -s -r -p "Restart the board in recovery mode then press any key to continue"
#sudo ./flash.sh -k kernel jetson-nano-2gb-devkit mmcblk0p1
