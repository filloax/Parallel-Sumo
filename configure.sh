#!/bin/bash

HAS_CLANG=$(command -v clang &>/dev/null)

cd build

if $HAS_CLANG; then
	echo "Using Clang 14..."
	cmake .. -G Ninja -D CMAKE_C_COMPILER=clang-14 -D CMAKE_CXX_COMPILER=clang++-14 "$@"
else
	echo "Using GCC 13..."
	cmake .. -G Ninja -D CMAKE_C_COMPILER=gcc-13 -D CMAKE_CXX_COMPILER=g++-13 "$@"
fi

