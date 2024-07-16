DESCRIPTION = "Root filesystem customization"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://etc/issue"
SRC_URI += "file://home/root/README.txt"
SRC_URI += "file://home/root/dione1280.py"
SRC_URI += "file://home/root/config"
SRC_URI += "file://etc/modules-load.d/g_serial.conf"

do_install() {
    install -d ${D}${sysconfdir}/modules-load.d
    install -d ${D}/home/root

    install -m 755 ${WORKDIR}/etc/issue ${D}${sysconfdir}
    install -m 755 ${WORKDIR}/home/root/* ${D}/home/root/
    install -m 755 ${WORKDIR}/etc/modules-load.d/* ${D}${sysconfdir}/modules-load.d/
}

FILES_${PN} += "${sysconfdir}"
FILES_${PN} += "/home/root"

RDEPENDS_${PN} += "bash python3-core"
