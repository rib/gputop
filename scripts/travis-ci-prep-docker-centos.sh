#!/bin/sh

set -e

if ! test $# -eq 2; then
    echo "usage: $0 docker-base-image-name new-image-name"
    exit 1
fi

_USER=`id -un`
_UID=`id -u`
_GID=`id -g`
_GROUP=`id -gn`

if test $_UID -eq 0; then
    echo "Not expected to be run as root"
    echo ""
    echo "This script should be run as the user that will"
    echo "be building gputop"
    exit 1
fi

cat << EOF > Dockerfile
FROM $1
USER root
RUN yum clean all
RUN yum update -y && yum install -y \
    make \
    sudo \
    pkg-config \
    gettext \
    python34-pip \
    python34-setuptools \
    python-lxml \
    python-mako \
    ncurses-devel \
    mesa-libGL-devel \
    openssl-devel \
    glfw-devel \
    libepoxy-devel \
    mesa-libEGL-devel \
    ninja-build \
    curl \
    psmisc \
    unzip \
    which && \
    yum clean all
RUN curl -sL https://rpm.nodesource.com/setup_6.x | sudo -E bash - && \
    yum install -y nodejs && \
    yum clean all
RUN ln -sf /usr/bin/ninja-build /usr/bin/ninja
RUN easy_install-3.4 pip
RUN pip3 install meson==0.44.0
RUN groupadd -g $_GID $_GROUP && useradd -u $_UID -g $_GID -G wheel -m $_USER
RUN echo "%sudo ALL=NOPASSWD: ALL">>/etc/sudoers

USER $_USER
RUN touch /home/$_USER/.sudo_as_admin_successfull
RUN mkdir /home/$_USER/build
WORKDIR /home/$_USER/build

CMD ["/bin/bash"]
EOF

echo "Deriving final $2 build image from base $1 image..."
echo ""
echo "  This is adding a '$_USER' user that matches the current host user"
echo "  to allow seamless mounting of host directories"
echo ""
docker build -t $2 .

cat << EOF > build.sh
sudo docker run -t -i -v \$PWD:/home/$USER/src $2 build/scripts/travis-ci-build.sh
EOF
chmod +x ./build.sh

echo ""
echo "Ready!"
echo ""
echo "To enter the build environment run:"
echo ""
echo "$ sudo docker run -t -i -v \$PWD:/home/$USER/build $2"
echo ""
echo "This will mount the host's current working directory in ~/build"
echo ""
