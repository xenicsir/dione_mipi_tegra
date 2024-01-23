#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

cd "$TEGRA_HOME"

sudo ./flash.sh --rcm-boot jetson-nano-2gb-devkit mmcblk0p1
