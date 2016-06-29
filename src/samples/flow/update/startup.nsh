# This file is part of the Soletta (TM) Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Example of an EFI startup script to be used with linux-micro-efi-update
# module. It checks existence of 'updating' file to determine that
# an update happened and creates a 'check-update' file that new version
# should delete when first started. If it fails for some reason, this script
# will start kernel using a backup as init program.

echo -off

echo Welcome to Soletta app

fs0:

if exist check-update then
    echo Soletta update failed? Trying backup one
    vmlinuz-linux.efi initrd=\initramfs-linux.img root=/dev/sda2 rw init=/usr/bin/soletta_old panic=5 S
endif

if exist updating then
    mv updating check-update
endif

vmlinuz-linux.efi initrd=\initramfs-linux.img root=/dev/sda2 rw init=/usr/bin/soletta panic=5 S
