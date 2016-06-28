#!/bin/bash

mkdir deploy
cd deploy
git config --global user.email "Travis@travis.org"
git config --global user.name "Travis CI"
git clone --quiet https://${GH_TOKEN}@github.com/gputop/gputop.github.io.git &> /dev/null
cd gputop.github.io
rm -rf *
cp -r $TRAVIS_BUILD_DIR/install/share/remote/* .
echo "disable jekyll">.nojekyll
echo "www.gputop.com" >> CNAME
git add --all
git status
git commit -m "Deployed by Travis"
git push origin master &> /dev/null
