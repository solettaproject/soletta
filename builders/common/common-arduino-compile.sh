#!/bin/bash

if [ -z "$COMPILE_DIR" ]; then
    echo Prepare script need to set COMPILE_DIR before including ${BASH_SOURCE[0]}
    exit 1
fi

rm -rf $COMPILE_DIR/RIOT/examples/soletta_app
cp -r $COMPILE_DIR/RIOT/examples/soletta_app_base $COMPILE_DIR/RIOT/examples/soletta_app

# TODO: better support for subdirectories in the build itself, the current code
# might have conflicts (plat-riot/info.c and plat-arm/info.c).
cp * */* $COMPILE_DIR/RIOT/examples/soletta_app/

make WERROR=0 -C $COMPILE_DIR/RIOT/examples/soletta_app

if [ $? -ne 0 ]; then
    exit 1
fi

mkdir $PLATFORM_NAME
