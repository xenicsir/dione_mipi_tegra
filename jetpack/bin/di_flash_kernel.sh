#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

if [ ! -d "$ROOTFS_MOUNT_PATH" ]; then
    echo "Enter uboot then run this command to mound sdcard:"
    echo "> ums 0 mmc 1"
    exit
fi

set -e

#IN_IMAGE="$TEGRA_HOME/kernel/Image_base"
IN_IMAGE="$TEGRA_KERNEL_OUT/arch/arm64/boot/Image"
OUT_IMAGE="$ROOTFS_MOUNT_PATH/boot/Image"

#md5sum "$OUT_IMAGE"
sudo cp "$IN_IMAGE" "$OUT_IMAGE"
sleep 1
sudo cp "$IN_IMAGE" "$OUT_IMAGE"
sudo sync "$ROOTFS_MOUNT_PATH"

echo "$IN_IMAGE"
echo "$OUT_IMAGE"
md5sum "$IN_IMAGE" "$OUT_IMAGE"

echo "Reset the board"
