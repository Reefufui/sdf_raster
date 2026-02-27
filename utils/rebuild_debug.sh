#!/bin/bash

SCRIPT_NAME=$(basename "$0")
DEBUG_BUILD_DIR_NAME=debug-build

echo "[$SCRIPT_NAME]: Deleting old build directory $DEBUG_BUILD_DIR_NAME..."
rm -rf $DEBUG_BUILD_DIR_NAME

echo "[$SCRIPT_NAME]: Configuring CMake project in $DEBUG_BUILD_DIR_NAME..."
cmake -B $DEBUG_BUILD_DIR_NAME -S . -DCMAKE_BUILD_TYPE=Debug

if [ $? -ne 0 ]; then
    echo "[$SCRIPT_NAME]: CMake configuration failed."
    exit 1
fi

if [[ "$(uname)" == "Darwin" ]]; then
    NUM_CORES=$(sysctl -n hw.ncpu)
elif [[ "$(uname)" == "Linux" ]]; then
    NUM_CORES=$(nproc)
else
    echo "[$SCRIPT_NAME]: OS unsupported."
    echo "[$SCRIPT_NAME]: Linux: cmake --build $DEBUG_BUILD_DIR_NAME -j$(nproc)"
    echo "[$SCRIPT_NAME]: macOS: cmake --build $DEBUG_BUILD_DIR_NAME -j$(sysctl -n hw.ncpu)"
    exit 1
fi

echo "[$SCRIPT_NAME]: Building CMake project."
cmake --build "$DEBUG_BUILD_DIR_NAME" -j$NUM_CORES

if [ $? -ne 0 ]; then
    echo "[$SCRIPT_NAME]: Build failed."
    exit 1
fi

echo "[$SCRIPT_NAME]: Build successful in $DEBUG_BUILD_DIR_NAME."

