#!/bin/bash

export YOCTO_DIR=~/yocto-tegra-dione
export BRANCH="dunfell"
mkdir ${YOCTO_DIR}
cd ${YOCTO_DIR}

git clone -b ${BRANCH} git://git.yoctoproject.org/poky.git poky-${BRANCH}
git clone -b ${BRANCH}-l4t-r32.5.0 https://github.com/madisongh/meta-tegra.git
git clone -b ${BRANCH} git://git.openembedded.org/meta-openembedded
git clone https://github.com/xenicsir/dione_mipi_tegra.git

source poky-${BRANCH}/oe-init-build-env build

cp ../dione_mipi_tegra/meta-xenics-tegra/local.conf.sample conf/local.conf

bitbake-layers add-layer ../meta-tegra/
bitbake-layers add-layer ../meta-openembedded/meta-oe/
bitbake-layers add-layer ../meta-openembedded/meta-python/
bitbake-layers add-layer ../meta-openembedded/meta-networking/
bitbake-layers add-layer ../dione_mipi_tegra/meta-xenics-tegra/meta/

bitbake core-image-weston

