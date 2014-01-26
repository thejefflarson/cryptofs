#! /usr/bin/env bash

mkdir -p ./one
mkdir -p ./two

echo 'pass' | ../build/cryptofs ./one ./two
sleep 1
cd ./two
dd if=/dev/zero of=./file bs=1 count=65535
echo '1' >> ./file
cd ..

umount ./two
rm -rf ./one
rm -rf ./two