FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://0001-dione-ir-add-dt.patch"
SRC_URI += "file://0001-dione-ir-add-module.patch"
SRC_URI += "file://0001-dione-ir-add-to-dt.patch"
SRC_URI += "file://0001-dione-ir-add-to-kernel.patch"
SRC_URI += "file://0001-dione-ir-make-default.patch"
SRC_URI += "file://0001-regmap-add-mixed-endian.patch"
SRC_URI += "file://0002-dione_ir.cfg"
SRC_URI += "file://0003-usb_g_serial.cfg"
SRC_URI += "file://0004-platform-tegra-camera.patch"
