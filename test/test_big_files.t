#! /usr/bin/env bash
# set -x
dir=`dirname $0`
. ${dir}/utils.sh
cd `dirname ${0}`

mkdir -p ./one
mkdir -p ./two
echo "1..3"
expect "echo 'pass' | ../build/cryptofs ./one ./two"
cd ./two
expect 'dd if=/dev/zero of=./file bs=1 count=65535'
expect 'echo "1" >> ./file'
cd ..

# umount ./two
# rm -rf ./one
# rm -rf ./two