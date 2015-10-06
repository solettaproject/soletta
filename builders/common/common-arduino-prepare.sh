if [ -z "$PREPARE_DIR" ]; then
    echo Prepare script need to set PREPARE_DIR before including ${BASH_SOURCE[0]}
    exit 1
fi

cd $COMPILE_DIR

git clone --branch 2015.09 https://github.com/RIOT-OS/RIOT.git
if [ $? -ne 0 ]; then
    exit 1
fi

mkdir -p RIOT/pkg/libsoletta
cp $PREPARE_DIR/Makefile.soletta RIOT/pkg/libsoletta/Makefile
cp $PREPARE_DIR/Makefile.include RIOT/pkg/libsoletta/

mkdir -p RIOT/examples/soletta_app_base
cp $PREPARE_DIR/Makefile.app RIOT/examples/soletta_app_base/Makefile

cp -r $COMPILE_DIR/RIOT/examples/soletta_app_base $COMPILE_DIR/RIOT/examples/soletta_app_dummy
cat > $COMPILE_DIR/RIOT/examples/soletta_app_dummy/main.c <<EOF
#include "sol-mainloop.h"
static void startup(void) { sol_quit(); }
static void shutdown(void) {}
SOL_MAIN_DEFAULT(startup, shutdown);
EOF
make -j $PARALLEL_JOBS WERROR=0 -C $COMPILE_DIR/RIOT/examples/soletta_app_dummy
if [ $? -ne 0 ]; then
    exit 1
fi

cp \
    $PREPARE_DIR/flash.sh \
    $PREPARE_DIR/../common/common-compile.sh \
    $PREPARE_DIR/../common/common-arduino-compile.sh \
    $PREPARE_DIR/compile \
    $COMPILE_DIR

chmod +x $COMPILE_DIR/compile
