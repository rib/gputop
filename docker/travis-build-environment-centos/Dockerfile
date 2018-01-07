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

CMD ["/bin/bash"]
