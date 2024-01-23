#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

scp "$PACKAGES_DIR/driver_package_r32.${L4T_VER}.tar.gz" "$SSH_ADDRESS:~"
