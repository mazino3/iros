#!/bin/bash

dd if=/dev/zero of=os_2.img bs=516096c count=100

losetup -o0 /dev/loop100 os_2.img
mke2fs -b1024 /dev/loop100

mkdir -p mnt
mount -text2 /dev/loop100 mnt

cp -r sysroot/* mnt
mkdir -p mnt/home/eloc
mkdir -p mnt/tmp
mkdir -p mnt/etc
touch mnt/etc/resolv.conf
echo "localhost 127.0.0.1" >> mnt/etc/hosts

umount /dev/loop100
losetup -d /dev/loop100

chmod 777 os_2.img