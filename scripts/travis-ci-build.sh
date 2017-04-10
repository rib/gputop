#!/bin/bash

set -e
set -x

if test -z "$TRAVIS_BUILD_DIR"; then
    TRAVIS_BUILD_DIR=$PWD
fi

export LLVM=/usr/local/llvm-fastcomp/bin

git clone --branch 1.37.3 --single-branch --depth 1 https://github.com/kripken/emscripten
export PATH="$PWD/emscripten:$PATH"
emcc -v

# libprotobuf build can be cached by Travis CI to save some build time
if test -d $TRAVIS_BUILD_DIR/protobuf-build/lib; then
    echo "Using existing libprotobuf build"
else
    git clone --single-branch --branch master --depth 1 https://github.com/google/protobuf $TRAVIS_BUILD_DIR/protobuf-src
    pushd $TRAVIS_BUILD_DIR/protobuf-src
    ./autogen.sh
    ./configure --prefix=$TRAVIS_BUILD_DIR/protobuf-build
    make -j8
    make install
    popd
fi
export PKG_CONFIG_PATH=$TRAVIS_BUILD_DIR/protobuf-build/lib/pkgconfig

# Fetch glext.h header including INTEL_performance_query enums
#$(mkdir GL && cd GL && wget https://raw.githubusercontent.com/rib/mesa/wip/rib/oa-next/include/GL/glext.h)


npm --version
node --version

NOCONFIGURE=1 ./autogen.sh

./configure $CONFIG_OPTS --prefix=$TRAVIS_BUILD_DIR/gputop-travis-build
make V=1
make V=1 install
