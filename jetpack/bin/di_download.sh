#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

mkdir -p "$DL_DIR"

if [ ! -f "$DL_DIR/$TOOLCHAIN_FILE" ]; then
    wget "$TOOLCHAIN_LINK" -O "$DL_DIR/$TOOLCHAIN_FILE"
fi

wget "$DRV_LINK" -O "$DL_DIR/$DRV_FILE"
wget "$SRC_LINK" -O "$DL_DIR/$SRC_FILE"

if [ -n "$ROOTFS_FILE" ]; then
    wget "$ROOTFS_LINK" -O "$DL_DIR/$ROOTFS_FILE"
fi
