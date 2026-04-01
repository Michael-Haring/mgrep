#!/bin/bash

GREEN="\033[38;5;10m"
RED="\033[38;5;9m"
RESET="\033[0m"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prog_d="$SCRIPT_DIR/../build-debug/mgrep"
prog_r="$SCRIPT_DIR/../build-release/mgrep"

tmpfile1=$(mktemp)
tmpfile2=$(mktemp)

normal_grep=(grep -r "int main(" "..")
cool_grep=("$prog_d" -rc "int main(" "..")
printf "$RED"
printf "Normal, single-threaded, boring, slow, ugly, unimpressive, ancient grep time:$RESET"
time "${normal_grep[@]}" | wc -l > $tmpfile1
printf "$GREEN \nmgrep:$RESET" 
time "${cool_grep[@]}" | wc -l > $tmpfile2

printf "\nnormal grep matches:\t"
cat $tmpfile1
printf "mgrep matches:\t\t"
cat $tmpfile2

ls $HOME
