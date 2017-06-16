#!/bin/bash

set -e
set -x

if test -z "$TRAVIS_BUILD_DIR"; then
    TRAVIS_BUILD_DIR=$PWD
fi

gcc --version
g++ --version

export LLVM=/usr/local/llvm-fastcomp/bin

git clone --branch 1.37.3 --single-branch --depth 1 https://github.com/kripken/emscripten
export PATH="$PWD/emscripten:$PATH"
emcc -v

# Fetch glext.h header including INTEL_performance_query enums
#$(mkdir GL && cd GL && wget https://raw.githubusercontent.com/rib/mesa/wip/rib/oa-next/include/GL/glext.h)


npm --version
node --version

NOCONFIGURE=1 ./autogen.sh

./configure $CONFIG_OPTS --prefix=$TRAVIS_BUILD_DIR/gputop-travis-build
make V=1
make V=1 install
