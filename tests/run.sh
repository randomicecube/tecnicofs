#!/bin/sh

TESTS=($(ls -d *.c))
RESET="\033[0m"
ORANGE="\033[0;33m"

for i in ${TESTS[@]}; do
    i=${i%.c}
    echo -e "${ORANGE}Running test $i${RESET}"
    ./$i
done