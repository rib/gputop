FROM centos:7
RUN yum update -y && \
    yum -y install epel-release && \
    yum update -y && \
    yum groupinstall -y 'Development Tools' && \
    yum install -y \
    git \
    cmake3 \
    python-libs \
    ninja-build \
    ca-certificates \
    python-mako && \
    yum clean all -y

# Build and install Clang with the fastcomp backend but delete the build/source
# at the end to minimize the size of the Docker layer
RUN git clone --branch 1.37.3 --single-branch --depth 1 https://github.com/kripken/emscripten-fastcomp && \
    git clone --branch 1.37.3 --single-branch --depth 1 https://github.com/kripken/emscripten-fastcomp-clang emscripten-fastcomp/tools/clang && \
    mkdir emscripten-fastcomp/build && \
    cd emscripten-fastcomp/build && \
    LDFLAGS="-s" CFLAGS="-Os" CXXFLAGS="-Os" cmake3 .. -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local/llvm-fastcomp -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD="X86;JSBackend" -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF && \
    ninja-build -j8 && \
    ninja-build install && \
    cd / && \
    rm -fr emscripten-fastcomp
ENV LLVM /usr/local/llvm-fastcomp/bin

CMD ["/bin/bash"]
