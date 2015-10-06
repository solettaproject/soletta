if [ -z "$COMPILE_DIR" ]; then
    echo Compile script need to set COMPILE_DIR before including ${BASH_SOURCE[0]}
    exit 1
fi

if [ -z "$SOLETTA_TARGET" ]; then
    echo Compile script need to set SOLETTA_TARGET before including ${BASH_SOURCE[0]}
    exit 1
fi

if [ ! -d "$SOLETTA_TARGET" ]; then
    echo Soletta target directory $SOLETTA_TARGET must exist before including ${BASH_SOURCE[0]}
    exit 1
fi

PLATFORM_NAME=$(echo $(basename $COMPILE_DIR) | sed -e 's/platform-//')
SOLETTA_HOST=$COMPILE_DIR/soletta-host/build/soletta_sysroot

if [ -n "$PARALLEL_JOBS" ]; then
    PARALLEL_JOBS=8
fi

trap '
    ret=$?;
    set +e;
    if [[ $ret -ne 0 ]]; then
	echo FAILED TO COMPILE >&2
    fi
    exit $ret;
    ' EXIT

trap 'exit 1;' SIGINT

function die() {
    echo "ERROR: $*" >&2
    exit 1
}

function generate-main-from-fbp() {
    local OPTS="-j $SOLETTA_TARGET/usr/share/soletta/flow/descriptions"
    local CONF_COUNT=$(find -name conf.json | wc -l)

    local SEARCH_FBP_DIRS=$(find -type d -exec echo -I{} \;)
    OPTS="$OPTS $SEARCH_FBP_DIRS"

    if [ $CONF_COUNT -eq 1 ]; then
	local CONF_FILE=$(find -name conf.json)
	OPTS="$OPTS -c $CONF_FILE"
    fi

    if [ $CONF_COUNT -gt 1 ]; then
	die "Found multiple conf files, but only one should be provided: $(find -name conf.json | tr '\n' ' ')"
    fi

    LD_LIBRARY_PATH=$SOLETTA_HOST/usr/lib $SOLETTA_HOST/usr/bin/sol-fbp-generator $OPTS main.fbp main.c
    if [ $? -ne 0  ]; then
	die "Failed generating main.c"
    fi
}

if [ ! -e main.c -a ! -e main.fbp ]; then
    die "Couldn't find program main: 'main.c' or 'main.fbp'"
fi

if [ -e main.c -a -e main.fbp ]; then
    die "Found both 'main.c' and 'main.fbp', only one should be provided"
fi

if [ -e main.fbp ]; then
    generate-main-from-fbp
fi
