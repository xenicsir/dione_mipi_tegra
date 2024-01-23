#!/bin/bash

export YOCTO_DIR=~/yocto-tegra-dione
export BRANCH="kirkstone"
mkdir ${YOCTO_DIR}
cd ${YOCTO_DIR}

git clone -b ${BRANCH} git://git.yoctoproject.org/poky.git poky-${BRANCH}
git clone -b ${BRANCH}-l4t-r32.7.x https://github.com/madisongh/meta-tegra.git
git clone -b ${BRANCH} git://git.openembedded.org/meta-openembedded
git clone https://github.com/xenicsir/dione_mipi_tegra.git

sed '/0003-Update-allocator-to-use-actual-frame-sizes.patch/ s/^/#/' -i meta-tegra/recipes-multimedia/gstreamer/gstreamer1.0-plugins-nvvidconv_1.*.bb

source poky-${BRANCH}/oe-init-build-env build

cp ../dione_mipi_tegra/yocto/kirkstone/meta/local.conf.sample conf/local.conf

bitbake-layers add-layer ../meta-tegra/
bitbake-layers add-layer ../meta-openembedded/meta-oe/
bitbake-layers add-layer ../meta-openembedded/meta-python/
bitbake-layers add-layer ../meta-openembedded/meta-networking/
bitbake-layers add-layer ../dione_mipi_tegra/yocto/kirkstone/meta/

bitbake core-image-weston
