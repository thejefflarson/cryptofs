#! /usr/bin/env bash
ntest=1

expect(){
  bash -c "cd `pwd` && ${1}" 2>/dev/null
  if [ $? -eq 0 ]
  then
    echo "ok ${1}"
  else
    echo "not ok ${1}"
  fi
}