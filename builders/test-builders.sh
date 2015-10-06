#!/bin/bash

trap 'exit 1;' SIGINT

if [ "$1" == '-h' ]; then
    printf "Usage: ./test-builders.sh [git-repo] [branch]

       If none parameter is provided, target soletta will default
       to this soletta repo, e.g. ../, on the current checked out
       branch. \n\n"

    exit 1
fi

if [[ -n "$1" ]]; then
    export SOLETTA_TARGET="$1"
else
    export SOLETTA_TARGET="${PWD}/.."
fi

if [[ -n "$2" ]]; then
    export SOLETTA_BRANCH="$2"
fi

for dir in */; do
    if [[ $dir =~ platform-* ]]; then
        $dir/prepare -t
    fi
done
