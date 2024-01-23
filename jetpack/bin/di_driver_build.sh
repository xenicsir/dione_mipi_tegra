#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

$SCRIPTPATH/di_populate_driver.sh
$SCRIPTPATH/di_build_kernel.sh
