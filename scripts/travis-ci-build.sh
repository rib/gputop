#!/bin/bash

set -e
set -x

if test -z "$TRAVIS_BUILD_DIR"; then
    TRAVIS_BUILD_DIR=$PWD
fi

gcc --version
g++ --version

# When the emscripten SDK is installed, we don't install the node and
# instead use the one from the SDK.
if [ -d /opt/emsdk-portable ]; then
export PATH="/opt/emsdk-portable/clang/e1.37.27_64bit:/opt/emsdk-portable/node/4.1.1_64bit/bin:/opt/emsdk-portable/emscripten/1.37.27:$PATH"
emcc -v
fi

# Fetch glext.h header including INTEL_performance_query enums
#$(mkdir GL && cd GL && wget https://raw.githubusercontent.com/rib/mesa/wip/rib/oa-next/include/GL/glext.h)


npm --version
node --version

NOCONFIGURE=1 ./autogen.sh

./configure $CONFIG_OPTS --prefix=$TRAVIS_BUILD_DIR/gputop-travis-build
make V=1
make V=1 install
