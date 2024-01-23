#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

$SCRIPTPATH/di_extract_jetson_tarballs.sh
$SCRIPTPATH/di_patch_kernel.sh
$SCRIPTPATH/di_populate_driver.sh
$SCRIPTPATH/di_defconfig.sh
$SCRIPTPATH/di_build_kernel.sh
$SCRIPTPATH/di_pack_driver.sh
