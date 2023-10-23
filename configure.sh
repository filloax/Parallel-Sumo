#!/bin/bash

HAS_CLANG=0 #$(command -v clang &>/dev/null)

cd build

if $HAS_CLANG; then
	echo "Using Clang..."
	cmake .. -G Ninja -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ "$@"
else
	cmake .. -G Ninja "$@"
fi

