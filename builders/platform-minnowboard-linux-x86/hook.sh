#!/bin/bash

# This file is part of the Soletta Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#   * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#   * Neither the name of Intel Corporation nor the names of its
#     contributors may be used to endorse or promote products derived
#     from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -x

pacman --root=$ROOTFS --noconfirm -S \
       icu

cp -r soletta-target/build/soletta_sysroot/* $ROOTFS/

if [[ -n $DRONE_PACKAGES_URL ]]; then
    wget "$DRONE_PACKAGES_URL/linux-minnow-drone-4.1.4-1-x86_64.pkg.tar.xz"
    wget "$DRONE_PACKAGES_URL/low-speed-spidev-minnow-drone-git-r15.2d52b1d-3-x86_64.pkg.tar.xz"
else
    echo "Could not download kernel and low-speed-spidev module. Please, set DRONE_PACKAGES_URL variable."
    exit 1
fi

pacman --root=$ROOTFS --noconfirm -U linux-minnow-drone-4.1.4-1-x86_64.pkg.tar.xz
pacman --root=$ROOTFS --noconfirm -U low-speed-spidev-minnow-drone-git-r15.2d52b1d-3-x86_64.pkg.tar.xz

cat > $ROOTFS/boot/loader/loader.conf <<EOF
default minnow-drone*
EOF

cat > $ROOTFS/boot/loader/entries/minnow-drone.conf <<EOF
title      Arch Linux
options    console=ttyS0,115200 console=tty0 rw quiet
linux      /vmlinuz-linux-minnow-drone
initrd     /initramfs-linux-minnow-drone.img
EOF

cat > $ROOTFS/etc/systemd/system/app.service <<EOF
[Unit]
Description=app

[Service]
ExecStart=/usr/bin/app

[Install]
WantedBy=multi-user.target
EOF

systemctl --root=$ROOTFS enable app.service
