#!/bin/bash

set -x

if test -z "$TRAVIS_BUILD_DIR"; then
    TRAVIS_BUILD_DIR=$PWD
fi

# When the emscripten SDK is installed, we don't install the node and
# instead use the one from the SDK.
if [ -d /opt/emsdk-portable ]; then
export PATH="/opt/emsdk-portable/clang/e1.37.27_64bit:/opt/emsdk-portable/node/4.1.1_64bit/bin:/opt/emsdk-portable/emscripten/1.37.27:$PATH"
emcc -v
fi

#export GPUTOP_TRAVIS_MODE=1

export PATH=$TRAVIS_BUILD_DIR/gputop-travis-build/bin:$PATH

if [ "${CONFIG_OPTS/'enable-node-clients'}" != "$CONFIG_OPTS" ]; then
    gputop --fake 2> travis_log &
    GPUTOP_PID=$!
    sleep 3
    gputop-csv -m RenderBasic -c "Timestamp,GpuCoreClocks" &
    GPUTOP_CSV_PID=$!
    sleep 10

    kill $GPUTOP_CSV_PID
    kill $GPUTOP_PID

    echo "Server Log:"
    cat travis_log

    grep -q "OpenStream request received" travis_log || exit 1
fi

#TODO: test web ui

echo "PASSED"
exit 0
