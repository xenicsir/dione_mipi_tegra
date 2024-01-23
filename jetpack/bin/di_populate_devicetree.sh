#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

cp "$DRIVER_SRC_DIR/"tegra210-camera-xenics-*.dtsi "$TEGRA_KERNEL_HW_HOME/nvidia/platform/t210/porg/kernel-dts/porg-platforms"

if [ "$L4T_VER" == 5.1 ]; then
	cp "$DRIVER_SRC_DIR/5.1/"tegra210-porg-camera-xenics-*.dtsi "$TEGRA_KERNEL_HW_HOME/nvidia/platform/t210/porg/kernel-dts/porg-platforms"
	cp "$DRIVER_SRC_DIR/5.1/"tegra210-porg-plugin-manager-xenics-dione-ir.dtsi "$TEGRA_KERNEL_HW_HOME/nvidia/platform/t210/porg/kernel-dts/porg-plugin-manager"

	if [ ! -f "$TEGRA_KERNEL_HW_HOME/dione-ir-add-to-dt.patch.done" ]; then
		patch -p 1 -i "$DRIVER_SRC_DIR/5.1/dione-ir-add-to-dt.patch" -d "$TEGRA_KERNEL_HW_HOME"
		touch "$TEGRA_KERNEL_HW_HOME/dione-ir-add-to-dt.patch.done"
	fi
else
	cp "$DRIVER_SRC_DIR/6.1/"tegra210-porg-camera-xenics-*.dtsi "$TEGRA_KERNEL_HW_HOME/nvidia/platform/t210/porg/kernel-dts/porg-platforms"
	cp "$DRIVER_SRC_DIR/6.1/"tegra210-p3448-all-p3449-0000-camera-dione-ir-*.dts "$TEGRA_KERNEL_HW_HOME/nvidia/platform/t210/porg/kernel-dts"
	cp "$DRIVER_SRC_DIR/6.1/"tegra210-p3448-common-dione-ir.dts "$TEGRA_KERNEL_HW_HOME/nvidia/platform/t210/porg/kernel-dts"

	if [ ! -f "$TEGRA_KERNEL_HW_HOME/dione-ir-add-to-dt.patch.done" ]; then
		patch -p 1 -i "$DRIVER_SRC_DIR/6.1/dione-ir-add-to-dt.patch" -d "$TEGRA_KERNEL_HW_HOME"
		touch "$TEGRA_KERNEL_HW_HOME/dione-ir-add-to-dt.patch.done"
	fi

	if [ ! -f "$TEGRA_KERNEL_HW_HOME/dione-ir-make-default.patch.done" ]; then
		patch -p 1 -i "$DRIVER_SRC_DIR/6.1/dione-ir-make-default.patch" -d "$TEGRA_KERNEL_HW_HOME"
		touch "$TEGRA_KERNEL_HW_HOME/dione-ir-make-default.patch.done"
	fi
fi

set +x
