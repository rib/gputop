Travis CI build machines don't have enough RAM to link LLVM and it's also very
difficult to be able to have a build of LLVM complete within the 50 minute time
limit.

This Dockerfile was used to create an Ubuntu based Docker image that we use on
Travis for building GPU Top that already contains a pre-built LLVM fastcomp
toolchain.

  sudo docker build -t rib1/gputop-travis-ci .

  docker login
  docker push rib1/gputop-travis-ci
