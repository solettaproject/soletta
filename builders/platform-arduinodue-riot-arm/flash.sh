#!/bin/bash                                                                                                                                                                                                                                                                         

usage() {
    echo "Usage: sudo ./$(basename $0) <device to be flashed, e.g., /dev/ttyACM0>" 1>&$1;
}

if [ -z "$1" ]; then
    echo "No device supplied, aborting..."
    usage 1
    exit 1
fi

if [ `uname` = "Linux" ]; then
    stty -F $1 raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
    ./bossac  -R -e -w -v -b ./*.hex
elif [ `uname` = "Darwin" ]; then
    stty -f $1 raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
    ./bossac_osx -R -e -w -v -b ./*.hex
else
    echo "CAUTION: No flash tool for your host system found!"
fi
