Use the following command to install Yocto dependencies on Ubuntu.
```sh
sudo apt-get install gawk wget git-core diffstat unzip texinfo gcc-multilib build-essential chrpath socat libsdl1.2-dev xterm
```

Set up some variables:
```sh
$ export YOCTO_DIR=/home/$user/yocto-tegra
$ export BRANCH="kirkstone"
$ mkdir ${YOCTO_DIR}
$ cd ${YOCTO_DIR}
```

Prepare the base:
```sh
$ git clone -b ${BRANCH} git://git.yoctoproject.org/poky.git poky-${BRANCH}
$ git clone -b ${BRANCH}-l4t-r32.7.x https://github.com/madisongh/meta-tegra.git
$ source poky-${BRANCH}/oe-init-build-env build
```

Remove gstreamer patch, otherwise, Dione 640 / 320 won't work, gstreamer crashes
```sh
$ sed '/0003-Update-allocator-to-use-actual-frame-sizes.patch/ s/^/#/' -i meta-tegra/recipes-multimedia/gstreamer/gstreamer1.0-plugins-nvvidconv_1.*.bb

$ source poky-${BRANCH}/oe-init-build-env build
```

Prepare additional layers:
```sh
$ cd ../
$ git clone git://git.openembedded.org/meta-openembedded -b dunfell
$ git clone git://github.com/xenicsir/dione_mipi_tegra.git
$ cp yocto/kirkstone/meta/local.conf.sample build/conf/local.conf
$ cd build
```

Select your machine

| Device | \<MACHINE\> |||
| --- | --- | --- | --- |
| TX1 	 | jetson-tx1-devkit |
| TX2 	 | jetson-tx2-devkit | jetson-tx2-devkit-tx2i | jetson-tx2-devkit-4gb* |
| XAVIER | jetson-agx-xavier-devkit |||
| NANO 	 | jetson-nano-devkit |	jetson-nano-devkit-emmc | jetson-nano-2gb-devkit* |
| XAVIER NX |	jetson-xavier-nx-devkit* | jetson-xavier-nx-devkit-emmc* |	jetson-xavier-nx-devkit-tx2-nx* |
| ORIN | jetson-agx-orin-devkit |

and edit `build/conf/local.conf` by setting
```sh
MACHINE ?= <MACHINE>
```
to the proper value.

Add the required layers:
```sh
$ bitbake-layers add-layer ../meta-tegra/
$ bitbake-layers add-layer ../meta-openembedded/meta-oe/
$ bitbake-layers add-layer ../yocto/kirkstone/meta/meta/
$ bitbake-layers add-layer ../meta-openembedded/meta-python/
$ bitbake-layers add-layer ../meta-openembedded/meta-networking/
```

Start the build:
```sh
$ bitbake core-image-weston
```

| \<IMAGE\> |
| --- |
| core-image-sato-dev |
| core-image-weston |
| core-image-base |

Fixing (eventual) libglvnd download issue:
```sh
git -c core.fsyncobjectfiles=0 clone --bare --mirror "https://gitlab.freedesktop.org/glvnd/libglvnd.git" gitlab.freedesktop.org.glvnd.libglvnd.git --progress
mv gitlab.freedesktop.org.glvnd.libglvnd.git downloads/git2/
```

When build has finished, create SD-card image:
```sh
$ cd tmp/deploy/images/jetson-nano-2gb-devkit
$ mkdir tmp
$ cd tmp
$ tar -vxf ../core-image-weston-jetson-nano-2gb-devkit.tegraflash.tar.gz
$ ./dosdcard.sh core-image-weston.img
```
`core-image-weston.img` uSD card image will be created.

Updating SPI flash of Jetson Nano:
- boot up Jetson Nano in recovery mode
- then in the same folder:
```sh
$ sudo ./doflash.sh --spi-only
```

Updating DTB only:
```sh
$ cd .../build/tmp/work-shared/L4T-tegra210-32.5.2-r0/Linux_for_Tegra
$ cp ../../../deploy/images/jetson-nano-2gb-devkit/tegra210-p3448-0003-p3542-0000.dtb .
$ sudo ./flash.sh -k DTB -d tegra210-p3448-0003-p3542-0000.dtb jetson-nano-2gb-devkit mmcblk0p1
```
