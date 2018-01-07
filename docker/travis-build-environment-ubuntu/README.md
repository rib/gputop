This Dockerfile was used to create an Ubuntu based Docker image that we use on
Travis for building GPU Top that already contains a pre-built LLVM fastcomp
toolchain.

  sudo docker build -t djdeath/gputop-travis-ci-ubuntu16.04 .

  docker login
  docker push rib1/gputop-travis-ci-ubuntu16.04
