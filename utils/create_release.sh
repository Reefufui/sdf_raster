#!/bin/bash

PROJECT_NAME="sdf_raster"
VERSION="1.0.0"
RELEASE_DIR_BASE="release-packages"

EXECUTABLE_PATH="release-build/bin/${PROJECT_NAME}"
ASSETS_PATH="release-build/assets"
LOGS_PATH="release-build/logs"

get_os_name() {
    case "$(uname -s)" in
        Linux*)  echo "linux";;
        Darwin*) echo "macos";;
        CYGWIN*|MINGW32*|MSYS*) echo "windows";;
        *)       echo "unknown_os"
    esac
}

OS_NAME=$(get_os_name)

ARCH_NAME=$(uname -m)
case "$ARCH_NAME" in
    x86_64) ARCH_NAME="amd64";;
    arm64)  ARCH_NAME="arm64";;
    aarch64) ARCH_NAME="arm64";;
    *)      ARCH_NAME="unknown_arch";;
esac

RELEASE_NAME="${PROJECT_NAME}-v${VERSION}-${OS_NAME}-${ARCH_NAME}"
PACKING_DIR="./${RELEASE_NAME}"
FINAL_RELEASE_DIR="./${RELEASE_DIR_BASE}"

echo "--- Creating Release Packages ---"
echo "Project: ${PROJECT_NAME}"
echo "Version: ${VERSION}"
echo "OS: ${OS_NAME}"
echo "Architecture: ${ARCH_NAME}"
echo "Release directory: ${FINAL_RELEASE_DIR}"
echo "---------------------------------"

if [ ! -f "${EXECUTABLE_PATH}" ]; then
    echo "Error: Executable '${EXECUTABLE_PATH}' not found."
    echo "Please ensure you have built the project and the executable exists."
    exit 1
fi

mkdir -p "${FINAL_RELEASE_DIR}"

echo "Creating temporary packing directory: ${PACKING_DIR}"
rm -rf "${PACKING_DIR}"
mkdir -p "${PACKING_DIR}"

echo "Copying executable: ${EXECUTABLE_PATH}"
cp "${EXECUTABLE_PATH}" "${PACKING_DIR}/"

if [ "${OS_NAME}" == "macos" ]; then
    chmod +x "${PACKING_DIR}/${PROJECT_NAME}"
fi

if [ -d "${ASSETS_PATH}" ]; then
    echo "Copying assets folder: ${ASSETS_PATH}"
    cp -R "${ASSETS_PATH}" "${PACKING_DIR}/"
else
    echo "Warning: Assets folder '${ASSETS_PATH}' not found. Skipping."
fi

if [ -f "README.md" ]; then
    echo "Copying README.md"
    cp README.md "${PACKING_DIR}/"
fi

if [ -f "LICENSE" ]; then
    echo "Copying LICENSE"
    cp LICENSE "${PACKING_DIR}/"
elif [ -f "LICENSE.md" ]; then
    echo "Copying LICENSE.md"
    cp LICENSE.md "${PACKING_DIR}/"
fi

ZIP_FILE="${FINAL_RELEASE_DIR}/${RELEASE_NAME}.zip"
echo "Creating .zip archive: ${ZIP_FILE}"
cd "${PACKING_DIR}" || exit
zip -r "../${ZIP_FILE}" ./* > /dev/null
cd - > /dev/null

TAR_GZ_FILE="${FINAL_RELEASE_DIR}/${RELEASE_NAME}.tar.gz"
echo "Creating .tar.gz archive: ${TAR_GZ_FILE}"
cd "${PACKING_DIR}" || exit
tar -czvf "../${TAR_GZ_FILE}" ./* > /dev/null
cd - > /dev/null

echo "Cleaning up temporary packing directory: ${PACKING_DIR}"
rm -rf "${PACKING_DIR}"

echo "--- Release packages created: ---"
ls -lh "${FINAL_RELEASE_DIR}/"

echo "Done!"
