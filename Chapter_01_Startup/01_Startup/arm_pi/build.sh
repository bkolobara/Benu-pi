#!/bin/sh

# usage: ./build.sh [qemu|clean]
#
# arm adaptation started with examples on:
#http://balau82.wordpress.com/2010/02/28/hello-world-for-bare-metal-arm-using-qemu/
#

PROJECT=hello.elf
TARGET=kernel.img

#Compile if required
if [ $# -eq 0 ] || ( [ $1 = "qemu" ] && [ ! -e $PROJECT ] ); then

ARMROOT="/opt/arm-gcc/bin"

CCFLAGS="-g -Wall"
ASFLAGS="-g"
LDFLAGS="-e arch_start -Ttext=0x10000"

CC=arm-none-eabi-gcc
AS=arm-none-eabi-as
LD=arm-none-eabi-ld
CP=arm-none-eabi-objcopy

#compile
$ARMROOT/$AS -c startup.S -o startup.o $ASFLAGS
$ARMROOT/$CC -c hello.c -o hello.o $CCFLAGS

#link
$ARMROOT/$LD startup.o hello.o -o $PROJECT $LDFLAGS

#kreate image
$ARMROOT/$CP $PROJECT -O binary $TARGET

#Copy kernel to sd card
cp $TARGET /Volumes/NO\ NAME/

#Eject sdcard
diskutil eject /dev/disk1

#if an error occured => exit
if [ ! $? -eq 0 ] ; then
	exit
fi

echo Created System image: $PROJECT

fi #"compile"

if [ $# -gt 0 ] && [ $1 = "qemu" ]; then
	echo Starting... \(press Ctrl+a then x to stop\)
	qemu-system-arm -M versatilepb -m 4M -nographic -kernel $PROJECT

elif [ $# -gt 0 ] && [ $1 = "cleanall" ]; then
	echo Cleaning...
	rm -rf *.o $PROJECT
fi
