#!/bin/sh
# Gets each dependency at a pinned commit into third-party/. An existing checkout at that
# commit stays.
set -eu
cd "$(dirname "$0")"

fetch() { # <directory> <repository> <commit>
    if [ "$(git -C "third-party/$1" rev-parse HEAD 2> /dev/null || true)" = "$3" ]; then
        return
    fi
    rm -rf "third-party/$1"
    git init -q "third-party/$1"
    git -C "third-party/$1" fetch -q --depth 1 "$2" "$3"
    git -C "third-party/$1" checkout -q FETCH_HEAD
}

# A pinned commit of the master branch.
fetch libtock-c https://github.com/tock/libtock-c d81b72a55a9d5eb690b3c6ae83abe20727c2f0db
