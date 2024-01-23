#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

cd "$TEGRA_HOME"

sudo ./flash.sh -r jetson-nano-qspi mmcblk0p1
