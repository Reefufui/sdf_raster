#!/bin/bash

BUILD_DIR="debug-build"

echo "Cleaning previous build directory: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"

echo "Creating build directory: ${BUILD_DIR}"
mkdir "${BUILD_DIR}"
cd "${BUILD_DIR}"

is_macos() {
    if [[ "$(uname -s)" == "Darwin" ]]; then
        return 0
    else
        return 1
    fi
}

if is_macos; then
    echo "(macOS detected) please, ensure g++-15 is installed."
    export CXX="/usr/local/bin/g++-15"
fi

echo "Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Debug

echo "Building the project with make -j 12..."
make -j 12

echo "Creating symlink for assets..."
ln -s "../assets"

echo "Build process finished successfully."
cd ..
exit 0

