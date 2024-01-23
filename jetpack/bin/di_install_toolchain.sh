#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

rm -Rf "$CROSS_COMPILE_HOME"
mkdir -p "$CROSS_COMPILE_HOME"
tar xf "$DL_DIR/$TOOLCHAIN_FILE" -C "$CROSS_COMPILE_HOME"
