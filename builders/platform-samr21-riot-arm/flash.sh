#!/bin/bash

if [ -n "$SERIAL" ]; then
    export OPENOCD_EXTRA_INIT="-c cmsis_dap_serial $SERIAL"
fi

export HEXFILE=soletta_app.hex
export OPENOCD_CONFIG=openocd.cfg

./openocd.sh flash
