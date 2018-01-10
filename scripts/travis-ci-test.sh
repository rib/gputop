#!/bin/bash

set -x

LANG=en_US.UTF-8
LANGUAGE=en_US:en
LC_ALL=en_US.UTF-8
LC_MESSAGES=en_US.UTF-8

export LANG LANGUAGE LC_ALL

if test -z "$TRAVIS_BUILD_DIR"; then
    TRAVIS_BUILD_DIR=$PWD
fi

#export GPUTOP_TRAVIS_MODE=1

export PATH=$TRAVIS_BUILD_DIR/travis-install/bin:$PATH

if [ "${CONFIG_OPTS/'webui=true'}" == "$CONFIG_OPTS" ]; then
    gputop --fake 2> travis_log &
    GPUTOP_PID=$!
    sleep 3
    gputop-wrapper -m RenderBasic -c "Timestamp,GpuCoreClocks" &
    GPUTOP_WRAPPER_PID=$!
    sleep 10

    kill $GPUTOP_WRAPPER_PID
    kill $GPUTOP_PID

    echo "Server Log:"
    cat travis_log

    grep -q "OpenStream request received" travis_log || exit 1
fi

#TODO: test web ui

echo "PASSED"
exit 0
