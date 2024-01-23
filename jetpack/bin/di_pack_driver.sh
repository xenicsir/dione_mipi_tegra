!/bin/bash

SCRIPTPATH="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
source "$SCRIPTPATH/../environments"

set -e
set -x

PACK_HOME="$TEGRA_KERNEL_OUT/driver_package"

rm -Rf "$PACK_HOME"
mkdir -p "$PACK_HOME"

############################
# pack dtb files and kernel
############################

mkdir -p "$PACK_HOME/boot"

DTB_LIST="
tegra210-p3448-0000-as-p3448-0003.dtb
tegra210-p3448-0000-p3449-0000-a00.dtb
tegra210-p3448-0000-p3449-0000-a01.dtb
tegra210-p3448-0000-p3449-0000-a02.dtb
tegra210-p3448-0000-p3449-0000-a02-hdmi-dsi.dtb
tegra210-p3448-0000-p3449-0000-b00.dtb
tegra210-p3448-0000-p3449-0000-b00-hdmi-dsi.dtb
tegra210-p3448-0002-p3449-0000-a02.dtb
tegra210-p3448-0002-p3449-0000-b00.dtb
tegra210-p3448-0003-p3542-0000.dtb
tegra210-p3448-0003-p3542-0000-hdmi-dsi.dtb
"

if [ "$L4T_VER" != 5.1 ]; then
	DTB_LIST+="
tegra210-p3448-all-p3449-0000-camera-dione-ir-dual.dtbo
tegra210-p3448-all-p3449-0000-camera-dione-ir-imx477.dtbo
tegra210-p3448-all-p3449-0000-camera-dione-ir-imx219.dtbo
tegra210-p3448-all-p3449-0000-camera-imx219-dual.dtbo
tegra210-p3448-all-p3449-0000-camera-imx477-dual.dtbo
tegra210-p3448-all-p3449-0000-camera-imx477-imx219.dtbo
tegra210-p3448-common-dione-ir.dtbo
tegra210-p3448-common-imx219.dtbo
tegra210-p3448-common-imx477.dtbo
"
fi

for dtb in $DTB_LIST; do
	cp "$TEGRA_KERNEL_OUT/arch/arm64/boot/dts/$dtb" "$PACK_HOME/boot"
done

if [ -f "$TEGRA_KERNEL_OUT/arch/arm64/boot/Image" ]; then
    cp "$TEGRA_KERNEL_OUT/arch/arm64/boot/Image" "$PACK_HOME/boot"
fi

cat << EOF > "$PACK_HOME/install_boot.sh"
#!/bin/sh

set -e
set -x

dtsfilename=\$(tr -d '\0' < "/proc/device-tree/nvidia,dtsfilename")
dtbfilename=\$(basename -s .dts "\$dtsfilename")

sudo cp -R boot /
sudo cp "/boot/\$dtbfilename.dtb" "/boot/dtb/kernel_\$dtbfilename.dtb"

if [ -f "/boot/kernel_\$dtbfilename.dtb" ]; then
    sudo cp "/boot/\$dtbfilename.dtb" "/boot/kernel_\$dtbfilename.dtb"
fi
EOF

chmod +x "$PACK_HOME/install_boot.sh"

#####################################
# pack dione_ir.ko in case of module
#####################################

DRIVER_FILE="$TEGRA_KERNEL_OUT/drivers/media/i2c/dione_ir.ko"

if [ -f "$DRIVER_FILE" ]; then
    mkdir -p "$PACK_HOME/lib/modules"
    cp "$TEGRA_KERNEL_OUT/drivers/media/i2c/dione_ir.ko" "$PACK_HOME/lib/modules"

    cat << EOF > "$PACK_HOME/install_module.sh"
#!/bin/sh

set -e
set -x

sudo cp lib/modules/dione_ir.ko /lib/modules/\$(uname -r)/kernel/drivers/media/i2c
sudo depmod -a
EOF

    chmod +x "$PACK_HOME/install_module.sh"
fi


if [ "$L4T_VER" != 5.1 ]; then
    cat << EOF > "$PACK_HOME/create_overlay_single_cam.sh"
#!/bin/sh

set -e
set -x

dtsfilename=\$(tr -d '\0' < "/proc/device-tree/nvidia,dtsfilename")
dtbfilename=\$(basename -s .dts "\$dtsfilename")

# equivalent of \$ sudo /opt/nvidia/jetson-io/jetson-io.py
sudo fdtoverlay -i "/boot/dtb/kernel_\$dtbfilename.dtb" -o "/boot/kernel_\$dtbfilename-user-custom.dtb" /boot/tegra210-p3448-common-dione-ir.dtbo

sudo python3 - << PYTHON_EOF
import sys
sys.path.append('/opt/nvidia/jetson-io')
from Linux.extlinux import add_entry
add_entry('/boot/extlinux/extlinux.conf',
         'JetsonIO', 'Custom Header Config: <CSI Camera DIONE-IR>',
         '/boot/kernel_\$dtbfilename-user-custom.dtb',
         True)
PYTHON_EOF
EOF

    chmod +x "$PACK_HOME/create_overlay_single_cam.sh"

    cat << EOF > "$PACK_HOME/create_overlay_dual_cam.sh"
#!/bin/sh

set -e
set -x

dtsfilename=\$(tr -d '\0' < "/proc/device-tree/nvidia,dtsfilename")
dtbfilename=\$(basename -s .dts "\$dtsfilename")

# equivalent of \$ sudo /opt/nvidia/jetson-io/jetson-io.py
sudo fdtoverlay -i "/boot/dtb/kernel_\$dtbfilename.dtb" -o "/boot/kernel_\$dtbfilename-user-custom.dtb" /boot/tegra210-p3448-all-p3449-0000-camera-dione-ir-dual.dtbo

sudo python3 - << PYTHON_EOF
import sys
sys.path.append('/opt/nvidia/jetson-io')
from Linux.extlinux import add_entry
add_entry('/boot/extlinux/extlinux.conf',
         'JetsonIO', 'Custom Header Config: <Camera DIONE-IR Dual>',
         '/boot/kernel_\$dtbfilename-user-custom.dtb',
         True)
PYTHON_EOF
EOF

    chmod +x "$PACK_HOME/create_overlay_dual_cam.sh"

else

    cat << EOF > "$PACK_HOME/set_fdt.sh"
#!/bin/sh

set -e
set -x

dtsfilename=\$(tr -d '\0' < "/proc/device-tree/nvidia,dtsfilename")
dtbfilename=\$(basename -s .dts "\$dtsfilename")

sudo python3 - << PYTHON_EOF
import sys
sys.path.append('/opt/nvidia/jetson-io')
from Linux.extlinux import add_entry
add_entry('/boot/extlinux/extlinux.conf',
         'primary', 'primary kernel',
         '/boot/dtb/kernel_\$dtbfilename.dtb',
         True)
PYTHON_EOF
EOF

    chmod +x "$PACK_HOME/set_fdt.sh"

fi

mkdir -p "$PACKAGES_DIR"
tar -czvpf "$PACKAGES_DIR/driver_package_r32.${L4T_VER}.tar.gz" -C "$TEGRA_KERNEL_OUT" driver_package
