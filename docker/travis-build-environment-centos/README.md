This Dockerfile was used to create a CentOS based Docker image that we
use on Travis for building GPU Top that already contains an Emscripten
toolchain.

  sudo docker build -t djdeath/gputop-travis-ci-centos .

  docker login
  docker push djdeath/gputop-travis-ci-centos7
