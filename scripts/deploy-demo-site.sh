#!/bin/bash

mkdir deploy
cd deploy
git config --global user.email "Travis@travis.org"
git config --global user.name "Travis CI"
git clone --quiet https://${GH_TOKEN}@github.com/gputop/gputop.github.io.git &> /dev/null
cd gputop.github.io
rm -rf *
cp ${TRAVIS_BUILD_DIR}/travis-build/gputop-ui/*.js ${TRAVIS_BUILD_DIR}/travis-build/gputop-ui/*.wasm .
if [ -d ${TRAVIS_BUILD_DIR}/travis-build/gputop-ui/gputop-ui.wasm.map ]; then
    cp ${TRAVIS_BUILD_DIR}/travis-build/gputop-ui/gputop-ui.wasm.map .
fi
if [ -d ${TRAVIS_BUILD_DIR}/travis-build/gputop-ui/gputop-ui.wast ]; then
    cp ${TRAVIS_BUILD_DIR}/travis-build/gputop-ui/gputop-ui.wast .
fi
cp ${TRAVIS_BUILD_DIR}/gputop-ui/*.html ${TRAVIS_BUILD_DIR}/gputop-ui/*.css ${TRAVIS_BUILD_DIR}/gputop-ui/favicon.ico .
echo "disable jekyll">.nojekyll
echo "www.gputop.com" >> CNAME
git add --all
git status
git commit -m "Deployed by Travis"
git push origin master &> /dev/null
