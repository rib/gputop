FROM ubuntu:16.04
RUN apt-get update -y && apt-get install -y --no-install-recommends --no-install-suggests \
    build-essential \
    git \
    cmake \
    ninja-build \
    python2.7-minimal \
    libpython2.7-stdlib \
    ca-certificates \
    python-mako && \
    apt-get clean

# Build and install Clang with the fastcomp backend but delete the build/source
# at the end to minimize the size of the Docker layer
RUN git clone --branch 1.37.3 --single-branch --depth 1 https://github.com/kripken/emscripten-fastcomp && \
    git clone --branch 1.37.3 --single-branch --depth 1 https://github.com/kripken/emscripten-fastcomp-clang emscripten-fastcomp/tools/clang && \
    mkdir emscripten-fastcomp/build && \
    cd emscripten-fastcomp/build && \
    LDFLAGS="-s" CFLAGS="-Os" CXXFLAGS="-Os" cmake .. -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local/llvm-fastcomp -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD="X86;JSBackend" -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF && \
    ninja -j8 && \
    ninja install && \
    cd / && \
    rm -fr emscripten-fastcomp
ENV LLVM /usr/local/llvm-fastcomp/bin

CMD ["/bin/bash"]
