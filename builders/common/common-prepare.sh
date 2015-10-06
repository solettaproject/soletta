if [ -z "$PREPARE_DIR" ]; then
    echo Prepare script need to set PREPARE_DIR before including ${BASH_SOURCE[0]}
    exit 1
fi

PLATFORM_NAME=$(echo $(basename $PREPARE_DIR) | sed -e 's/platform-//')
COMPILE_DIR=$PREPARE_DIR/../out/platform-$PLATFORM_NAME

if [ -n "$PARALLEL_JOBS" ]; then
    PARALLEL_JOBS=8
fi

trap '
    ret=$?;
    set +e;
    if [[ $ret -ne 0 ]]; then
	echo FAILED TO PREPARE >&2
    fi
    exit $ret;
    ' EXIT

trap 'exit 1;' SIGINT

rm -rf $COMPILE_DIR
mkdir -p $COMPILE_DIR
cd $COMPILE_DIR

git clone https://github.com/solettaproject/soletta.git soletta-host
if [ $? -ne 0 ]; then
    exit 1
fi

pushd soletta-host

make alldefconfig
if [ $? -ne 0 ]; then
    exit 1
fi

make -j $PARALLEL_JOBS build/soletta_sysroot/usr/bin/sol-fbp-generator
if [ $? -ne 0 ]; then
    exit 1
fi

popd
