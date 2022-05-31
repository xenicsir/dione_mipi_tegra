DESCRIPTION = "v4l2grab" 
SECTION = "examples" 
LICENSE = "CLOSED" 
PR = "r0" 

DEPENDS += "libjpeg-turbo libv4l"

FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}-${PV}:"

SRC_URI = "git://github.com/twam/v4l2grab.git"
SRCREV = "master"

S = "${WORKDIR}/git"

inherit autotools

