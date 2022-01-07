#!/bin/sh

TESTS=($(ls -d *.c))

for i in ${TESTS[@]}; do
    i=${i%.c}
    echo "Running test $i"
    ./$i
done