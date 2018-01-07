FROM ubuntu:16.04
RUN apt-get update -y && apt-get install -y --no-install-recommends --no-install-suggests \
    build-essential \
    curl \
    git \
    cmake \
    ninja-build \
    python2.7-minimal \
    libpython2.7-stdlib \
    ca-certificates \
    python-mako && \
    apt-get clean

# Download emscripten
RUN mkdir -p /opt
RUN cd /opt && curl https://s3.amazonaws.com/mozilla-games/emscripten/releases/emsdk-portable.tar.gz | tar xvz
RUN cd /opt/emsdk-portable && ./emsdk install sdk-1.37.27-64bit
RUN chmod go+rX -R /opt/emsdk-portable

CMD ["/bin/bash"]
