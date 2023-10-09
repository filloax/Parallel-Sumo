#!/bin/bash

# Update the libraries used in compiling

root_dir="$( realpath "$( dirname "${BASH_SOURCE[0]}" )/..")"

function fix_win_mingw_defines() {
    if [ $# -ne 1 ]; then
        echo "Usage: fix_win_mingw_defines <filename>"
        return 1
    fi

    local filename="$1"

    replace_with="//#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__MSYS__)"

    find_patterns=(
        "#if defined(_WIN32)"
        "#if defined(WIN32)"
        "#ifdef _WIN32"
        "#ifdef WIN32"
        "#if defined(_WIN64)"
        "#if defined(WIN64)"
        "#ifdef _WIN64"
        "#ifdef WIN64"
    )
    for _find in "${find_patterns[@]}"; do
        sed -i "/$_find/i $replace_with" "$filename"
    done

    replace_with_negated="//#if !( defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__MSYS__) )"

    find_patterns_negated=(
        "#if !defined(_WIN32)"
        "#if !defined(WIN32)"
        "#ifndef _WIN32"
        "#ifndef WIN32"
        "#if !defined(_WIN64)"
        "#if !defined(WIN64)"
        "#ifndef _WIN64"
        "#ifndef WIN64"
    )
    for _find in "${find_patterns_negated[@]}"; do
        sed -i "/$_find/i $replace_with_negated" "$filename"
    done
}

function download_repo() {
    local repo="$1"
    shift
    local branch="$1"
    shift
    local subdir="$1"
    shift
    local files=("$@")

    for file in "${files[@]}"; do
        url="https://raw.githubusercontent.com/$repo/$branch/$file"
        output_file="$root_dir/libs/$subdir/$(basename "$file")"
        
        # echo "curl -sS -o $output_file -L $url"

        # Download the file using curl
        curl  -sS -o "$output_file" -L "$url"
        
        if [ $? -eq 0 ]; then
            fix_win_mingw_defines "$output_file"
            echo "Downloaded: $file"
        else
            echo "Failed to download: $file"
        fi
    done
}

mkdir -p "$root_dir/libs" 2>/dev/null
mkdir -p "$root_dir/libs/traciapi/foreign/tcpip" 2>/dev/null
mkdir -p "$root_dir/libs/traciapi/libsumo" 2>/dev/null

echo Updating tinyxml2...
files_tinyxml=(
  "tinyxml2.h"
  "tinyxml2.cpp"
)
download_repo leethomason/tinyxml2 master . "${files_tinyxml[@]}"
echo Updated tinyxml2

echo Updating argparse...
files_argparse=(
  "include/argparse/argparse.hpp"
)
download_repo p-ranav/argparse master . "${files_argparse[@]}"
echo Updated argparse

echo Updating TraCIAPI...
echo "//Needed for SUMO compilation" > "$root_dir/libs/traciapi/config.h"
files_traciapi=(
  "src/utils/traci/TraCIAPI.h"
)
files_traciapi_foreign=(
  "src/foreign/tcpip/socket.h"
  "src/foreign/tcpip/socket.cpp"
  "src/foreign/tcpip/storage.h"
  "src/foreign/tcpip/storage.cpp"
)
download_repo eclipse-sumo/sumo main traciapi "${files_traciapi[@]}" 
download_repo eclipse-sumo/sumo main traciapi/foreign/tcpip "${files_traciapi_foreign[@]}" 
echo Updated TraCIAPI

echo Remember to check the \#if _WIN32 etc. defines! Comments were added with suggestions
echo on how to change them, but they might not always be needed/work