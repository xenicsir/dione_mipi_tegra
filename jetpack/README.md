---
header-includes:
  - \hypersetup{colorlinks=true,
            allbordercolors={0 0 0},
            pdfborderstyle={/S/U/W 1}}
---

# Introduction

This repository contains a driver for the dione-ir infra sensor which
works on the NVIDIA Jetson Nano 2GB board. This document describes the
preparation of the environment and building of the kernel included the
dione-ir driver.


# Prerequisites


## Hardware

* dione-ir evaluation kit, which includes

  * NVIDIA Jetson Nano 2GB development board
  * Dione sensor with cable

* microSD card (minimum 32GB UHS-1 required, 64Gb recommended)
* USB-C cable with charger 5V / 2A minimum, 3A recommended
* monitor with an HDMI cable
* network connection with UTP cable or WIFI
* micro USB cable
* USB-Serial converter with 3.3V I/O, our recommendation is
[SparkFun FTDI Basic Breakout - 3.3V](https://www.sparkfun.com/products/9873)
* buttons with cables or maybe jumpers for reset and recovery mode


## Software
Ubuntu/Debian Linux operating system (64bit) on a **host** computer

**Note:**

* under VirtualMachine the system cannot recognize the Jetson recovery mode
properly, so do not use VM
* we recommend using the latest Ubuntu 20.04 or 20.10


# Jetson Nano bring-up

Please follow the [link](https://developer.nvidia.com/embedded/learn/get-started-jetson-nano-2gb-devkit)
to bring-up the Jetson Nano board, then upgrade the Linux and install v4l-utils.

**Warning:** You need to use the same version of the sd-card image as this
building environment. The actual version of the L4T is 32.6.1 and the release
date is 2021/08/04. You should use the sd-card image that is released on the
same day, which version is 4.6 You can find the releases on the
[Jetson Download Center](https://developer.nvidia.com/embedded/downloads).


## Upgrade the Linux

```
$ sudo apt update
$ sudo apt upgrade
```


## Install v4l-utils

```
$ sudo apt install v4l-utils
```


# The hardware setup of Jetson Nano for developing

Study this document:
[Jetson Nano 2GB Developer Kit User Guide](https://developer.nvidia.com/embedded/learn/jetson-nano-2gb-devkit-user-guide)

* connect your USB-Serial module to the 12-Pin Button Header (J12)
* add buttons (or maybe jumpers) for reset and recovery mode,
connect the buttons to the J12
* connect a micro USB cable to the J13
* connect the Dione camera to the J5


# Prepare the host computer for building the kernel


## Install the following packages

Install minicom for the serial communication and some packages for kernel
building:

```
$ sudo apt install minicom
$ sudo apt install build-essential bc
```


## Add necessary lines into your .bashrc


### Add bin/ folder to the path

```
export PATH=$PATH:~/<path_of_the_repo>/bin
```

**Note:** If you want to avoid editing .bashrc, all programs can be called from
the ```bin/``` directory.


### Specify ssh address of the nano board

```
export SSH_ADDRESS=nanouser@nanohost
```


### Specify your USB-Serial converter device path

```
export SERIAL_DEVICE=/dev/ttyUSB0
```


### Specify the sd card mount path

For this, you need to enter the Jetson Nano board into recovery mode, we can
skip this step right now, we will return to this later:

```
export ROOTFS_MOUNT_PATH=/media/<youruser>/<mountpoint>
```


## Customizing the environments file

### Choose Jetson Linux Driver Package (L4T)

```
#export L4T_VER=5.1
export L4T_VER=6.1
#export L4T_VER=7.1
```

### Enable downloading and extracting the sample rootfs (optional)

Uncomment this, if you need the sample rootfs for further development:

```
export ROOTFS_FILE="tegra_linux_sample-root-filesystem_r32.${L4T_VER}_aarch64.tbz2"
```


### Add parameters in the environmets file (optional)

Specify the previous variables here, if you don't want to edit your .bashrc:

```
export SSH_ADDRESS="${SSH_ADDRESS:-user@nano2gb}"
export SERIAL_DEVICE="${SERIAL_DEVICE:-/dev/ttyUSB0}"
export ROOTFS_MOUNT_PATH="${ROOTFS_MOUNT_PATH:-/media/<youruser>/<mountpoint>}"
```


## Download & extract & build the kernel

Run the following commands in this order:

* ```$ di_download.sh```
downloads the necessary files, the output directory is ```download/```
* ```$ di_install_toolchain.sh```
extracts the toolchain to the ```l4t-gcc/``` directory
* ```$ di_extract_jetson_tarballs.sh```
extracts the driver and sources (optionally the sample rootfs) to the
```Linux_for_Tegra/``` folder
* ```$ di_patch_kernel.sh```
patches the kernel, the paths are in ```kernel_patches/``` folder
* ```$ di_defconfig.sh```
runs the defconfig, configures the kernel for tegra
* ```$ di_build_kernel.sh```
builds the kernel

Now we have a built kernel without the dione-ir driver, let's patch the kernel.


## Add dione-ir kernel driver

* ```$ di_populate_driver.sh```:
the script will copy the driver files from the ```driver_src/``` folder to the
right places, and patches the necessary files (Makefile, config, etc.)
  * ```$ di_defconfig.sh```: running this script is only necessary for the first time,
  because ```di_populate_devicetree.sh``` script will add new Kernel config for the driver
  * ```$ di_populate_devicetree.sh```: this script installs the device tree files,
  however you don't need to run it, since ```di_populate_driver.sh``` will call it
* ```$ di_build_kernel.sh``` builds the kernel

**Note:** For the faster development cycle, there is a shortcut that runs the
two scripts at once (populate driver and build kernel steps):
```
$ di_driver_build.sh
```


# Flashing the built kernel

To get to work the Dione sensor, you need to flash the compiled kernel and device
tree to the board.

Unfortunately, we need two different steps to flash the dtb and the kernel
image. For the device tree, we need to turn the board in recovery mode. For
flashing the kernel image, we need to enter the U-Boot.


## Preparation

Please test the ```di_serial.sh``` and ```di_ssh.sh``` scripts.
You need to see the login prompt in both cases if the board properly booted.


### Serial troubleshooting

For serial communication, we use the minicom util program. If you have any
problem, please check the ```$SERIAL_DEVICE``` variable defined previously or
check the minicom settings in the minicom program. In my case, I had to disable
the hardware flow control, for example.


### SSH troubleshooting

In case of any problem, please check the ```$SSH_ADDRESS``` variable, the
username and hostname must match what you specified during board initialization.


## Flashing the device tree

For this step, you need to enter the recovery mode, then run the
```$ di_flash_dtb.sh``` command.

Follow these steps:

* optionally restart the board, if you want to avoid resetting a running system:
```$ sudo reboot```
* push the reset and recovery mode button at the same time
* first, release the reset button
* then release the recovery mode button
* finally check the USB device on the host machine with ```$ lsusb```,
you need to see something similar:
```
Bus 001 Device 009: ID 0955:7f21 NVIDIA Corp.
```
* run the ```$ di_flash_dtb.sh``` script to flash dtb
* if done, the board will automatically restart


## Flashing the kernel

For this step, you have to enter the U-Boot to mount the sd card to the host
Linux.

Follow these steps:

* connect a micro USB cable to the board (J13)
* open serial console ```$ di_serial.sh```
* reboot the system ```$ sudo reboot```
* press a key at this line ```Hit any key to stop autoboot:  2```
* you must see the U-Boot prompt ```Tegra210 (P3541-0000) #```
* enter the following command to mount the sd card image ```# ums 0 mmc 1```
  * for the first time, check the mounted filesystem and edit variable
```export ROOTFS_MOUNT_PATH=...```
* run the ```$ di_flash_kernel.sh``` script to copy the kernel Image
* press CTRL-C, then enter ```# boot``` command in the U-Boot, for booting


# Checking the Dione sensor on the Jetson Nano


## Check the kernel log

```
$ sudo dmesg | grep dione_ir
[    1.256113] dione_ir 6-000e: tegracam sensor driver:dione_ir_v2.0.6
[    1.279647] dione_ir 6-000e: detected dione_ir sensor
[    1.430000] vi 54080000.vi: subdev dione64 6-000e bound

```


## Check v4l2-compliance

Run the command and check the output:

```
$ v4l2-compliance -d /dev/video0
v4l2-compliance SHA   : not available

Driver Info:
	Driver name   : tegra-video
	Card type     : vi-output, imx219 6-000e
	Bus info      : platform:54080000.vi:0
	Driver version: 4.9.201
	Capabilities  : 0x84200001
		Video Capture
		Streaming
		Extended Pix Format
		Device Capabilities
	Device Caps   : 0x04200001
		Video Capture
		Streaming
		Extended Pix Format

...
```


## Check available formats

```
$ v4l2-ctl -V -d /dev/video0
Format Video Capture:
	Width/Height      : 640/480
	Pixel Format      : 'AR24'
	Field             : None
	Bytes per Line    : 2560
	Size Image        : 1228800
	Colorspace        : sRGB
	Transfer Function : Default (maps to sRGB)
	YCbCr/HSV Encoding: Default (maps to ITU-R 601)
	Quantization      : Default (maps to Full Range)
	Flags             :
```


## Capture a raw image

```
$ v4l2-ctl --set-fmt-video=width=640,height=480,pixelformat=AR24 --stream-mmap --stream-count=1 -d /dev/video0 --stream-to=dione640.raw
```


## GStreamer live stream

```
$ gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGRA,width=640,height=480 ! videoconvert ! video/x-raw,format=NV12 ! nvvidconv ! nvoverlaysink sync=false
```

### For other sensors

```
$ gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGRA,width=1280,height=1024 ! videoconvert ! video/x-raw,format=NV12 ! nvvidconv ! nvoverlaysink sync=false
$ gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGRA,width=320,height=240 ! videoconvert ! video/x-raw,format=NV12 ! nvvidconv ! nvoverlaysink sync=false
$ gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGRA,width=1024,height=768 ! videoconvert ! video/x-raw,format=NV12 ! nvvidconv ! nvoverlaysink sync=false

```

## Test pattern generation


### Enable test mode

Edit dione_ir.c:

```
static int test_mode = 1;
```

then compile and flash the kernel.

**Note:** This is the simplest way to enable the test pattern generation,
but the correct way would be:

* compile the dione_ir as a module, and load it with test_mode=1 parameter
* in the case of a static module, you can specify a module parameter in the kernel
command line, see:
[ways-of-sending-parameters-to-static-kerenel-module](https://stackoverflow.com/questions/28600004/ways-of-sending-parameters-to-static-kerenel-module)

However, these solutions are not the scope of this documentation.


### Test pattern live stream

```
$ gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGRA,width=640,height=480 ! videoconvert ! video/x-raw,format=NV12 ! nvvidconv ! nvoverlaysink sync=false
```

# Known issues


## Device id in the compatible string is limited to 7 characters long

Currently, ```dioneir``` is used as a compatible string because ```dione_ir```
will crash the kernel, probably there is a strcpy() bug somewhere.


## Kernel Image must be copied twice to the board

In my experience, I had to copy the kernel Image twice in the
```$ di_flash_lernel.sh``` script otherwise, the previous kernel will boot
the first time, and only at the second boot will be the flashed kernel
actualized.

Unfortunately, even so, the kernel may not update properly until after a reboot.

## Yocto Linux stuck recovery mode issue

If your board previously flashed with yocto linux, you need to reflash it, otherwise it may stuck in recovery mode after boot.

* ```$ di_restart.sh```: start the board in stuck recovery mode
* ```$ di_reflash_board.sh```: re-flash the board (in recovery mode) to fix this issue


# Resources

[Flashing](https://docs.nvidia.com/jetson/l4t/index.html#page/Tegra%20Linux%20Driver%20Package%20Development%20Guide/flashing.html#)

[Kernel customization](https://docs.nvidia.com/jetson/l4t/index.html#page/Tegra%20Linux%20Driver%20Package%20Development%20Guide/kernel_custom.html#)

[Camera development](https://docs.nvidia.com/jetson/l4t/index.html#page/Tegra%20Linux%20Driver%20Package%20Development%20Guide/camera_dev.html#)

[tc358748.c](https://github.com/avionic-design/linux-l4t/blob/meerkat/l4t-r21-5/drivers/media/i2c/tc358748.c)

[tc358746: add Toshiba TC358746 Parallel to CSI-2 bridge driver](https://patchwork.kernel.org/project/linux-media/patch/20190619152838.25079-3-m.felsch@pengutronix.de/)
