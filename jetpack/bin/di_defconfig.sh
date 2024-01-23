#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

rm -Rf "$TEGRA_KERNEL_OUT"
mkdir -p "$TEGRA_KERNEL_OUT"
cd "$TEGRA_KERNEL_HOME"
make ARCH=arm64 O="$TEGRA_KERNEL_OUT" tegra_defconfig
