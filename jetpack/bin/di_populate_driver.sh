#!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e

$SCRIPTPATH/di_populate_devicetree.sh

set -x
# add source files
cp "$DRIVER_SRC_DIR/dioneir.c" "$TEGRA_KERNEL_NV_HOME/nvidia/drivers/media/i2c"
cp "$DRIVER_SRC_DIR/"tc358746*.[hc] "$TEGRA_KERNEL_NV_HOME/nvidia/drivers/media/i2c"

if [ ! -f "$TEGRA_KERNEL_HOME/regmap-add-mixed-endianness.patch.done" ]; then
    patch -p 1 -i "$DRIVER_SRC_DIR/regmap-add-mixed-endianness.patch" -d "$TEGRA_KERNEL_HOME"
    touch "$TEGRA_KERNEL_HOME/regmap-add-mixed-endianness.patch.done"
fi

set +x

# patch configs and makefile
CONFIG="$TEGRA_KERNEL_NV_HOME/nvidia/drivers/media/i2c/Kconfig"
MAKEFILE="$TEGRA_KERNEL_NV_HOME/nvidia/drivers/media/i2c/Makefile"
DEFCONFIG="$TEGRA_KERNEL_HOME/arch/arm64/configs/tegra_defconfig"

# add dione-ir config to the Kconfig
awk '
BEGIN {
  found=0;
}
/DIONE_IR/ {
  found=1;
}
/^endmenu$/ {
  if (!found) {
    print "config VIDEO_DIONE_IR";
    print "\ttristate \"DIONE IR camera sensor support\"";
    print "\tdepends on I2C && VIDEO_V4L2 && VIDEO_V4L2_SUBDEV_API";
    print "\t---help---";
    print "\t  This driver supports DIONE (640/1280/320/1024) infra camera sensors from Xenics";
    print "";
    print "\t  To compile this driver as a module, choose M here: the module";
    print "\t  will be called dione_ir.";
    print "";
  }
}
// {
  print;
}' "$CONFIG" > "$CONFIG.temp" && mv "$CONFIG.temp" "$CONFIG"

# add dione_ir module to the Makefile
awk '
BEGIN {
  found=0;
}
/DIONE_IR/ {
  found=1;
}
// {
  print
}
END {
  if (!found)
    print("obj-$(CONFIG_VIDEO_DIONE_IR) += dione_ir.o");
    print("dione_ir-y += dioneir.o tc358746_calculation.o");
}' "$MAKEFILE" > "$MAKEFILE.temp" && mv "$MAKEFILE.temp" "$MAKEFILE"

# append dione_ir config to the defconfig
awk '
BEGIN {
  found=0;
}
/DIONE_IR/ {
  found=1;
}
END {
  if (!found)
    print("CONFIG_VIDEO_DIONE_IR=m");
}
// {
  print;
}' "$DEFCONFIG" > "$DEFCONFIG.temp" && mv "$DEFCONFIG.temp" "$DEFCONFIG"
