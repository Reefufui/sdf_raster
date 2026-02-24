#!/bin/bash
# --- Конфигурация ---
INSTALL_BIN_DIR="/usr/local/bin" # Целевая директория для исполняемого файла slangc
INSTALL_LIB_DIR="/usr/local/lib" # Целевая директория для библиотек slangc
# Если вы не хотите устанавливать в системные директории, используйте:
# INSTALL_BIN_DIR="$HOME/.local/bin"
# INSTALL_LIB_DIR="$HOME/.local/lib"
# Важно: если используете ~/.local/bin и ~/.local/lib, убедитесь, что они в вашем PATH и LD_LIBRARY_PATH соответственно.
# Обычно это настраивается в ~/.profile или ~/.bashrc.

SLANG_REPO="shader-slang/slang"
RELEASE_TAG="" # Можно указать конкретный тег, например "v0.26.24", или оставить пустым для последней версии.

# --- Функции для вывода сообщений ---
log_info() {
    echo "INFO: $1"
}
log_warn() {
    echo "WARNING: $1" >&2
}
log_error() {
    echo "ERROR: $1" >&2
    exit 1
}

# --- Проверка зависимостей ---
check_dependency() {
    if ! command -v "$1" &> /dev/null; then
        log_error "Dependency '$1' not found. Please install it (e.g., 'sudo apt install $1' on Ubuntu, 'brew install $1' on macOS)."
    fi
}
check_dependency "curl"
check_dependency "jq" # Для парсинга JSON
check_dependency "uname"
check_dependency "grep"

# --- Определение ОС и архитектуры ---
OS_NAME=$(uname -s)
ARCH_NAME=$(uname -m)

log_info "Detected OS: $OS_NAME, Architecture: $ARCH_NAME"

TARGET_OS=""
TARGET_ARCH=""
TARBALL_EXT="tar.gz" # Предпочитаем tar.gz

case "$OS_NAME" in
    Linux*)
        TARGET_OS="linux"
        check_dependency "tar"
        ;;
    Darwin*)
        TARGET_OS="macos"
        check_dependency "tar"
        ;;
    *)
        log_error "Unsupported OS: $OS_NAME. This script supports Linux and macOS."
        ;;
esac

case "$ARCH_NAME" in
    x86_64)
        TARGET_ARCH="x86_64"
        ;;
    arm64|aarch64)
        TARGET_ARCH="aarch64"
        ;;
    *)
        log_error "Unsupported architecture: $ARCH_NAME. This script supports x86_64 and arm64/aarch64."
        ;;
esac

# --- Получение информации о последнем релизе ---
API_URL="https://api.github.com/repos/${SLANG_REPO}/releases"
if [[ -z "$RELEASE_TAG" ]]; then
    API_URL="${API_URL}/latest"
else
    API_URL="${API_URL}/tags/${RELEASE_TAG}"
fi

log_info "Fetching release info from: $API_URL"
RELEASE_DATA=$(curl -s "$API_URL")

# Проверка на ошибки curl или jq
if [[ $? -ne 0 ]]; then
    log_error "Failed to fetch release data from GitHub API. Check your internet connection or the repository name."
fi
if echo "$RELEASE_DATA" | grep -q "message.:.Not.Found"; then
    log_error "GitHub API returned 'Not Found'. Release tag '${RELEASE_TAG}' might not exist, or repository name is incorrect."
fi
if ! echo "$RELEASE_DATA" | jq -e . > /dev/null; then
    log_error "Failed to parse JSON response from GitHub API. Response might be malformed or empty."
fi

LATEST_VERSION=$(echo "$RELEASE_DATA" | jq -r .tag_name)
ASSETS=$(echo "$RELEASE_DATA" | jq -c '.assets[]')

if [[ -z "$LATEST_VERSION" || "$LATEST_VERSION" == "null" ]]; then
    log_error "Could not determine latest release version."
fi

log_info "Found latest Slang version: $LATEST_VERSION"

# --- Поиск подходящего файла для скачивания ---
DOWNLOAD_URL=""
FILENAME_PATTERN="${TARGET_OS}-${TARGET_ARCH}"
GLIBC_VERSION="" # Для более старых Linux систем может потребоваться
DOWNLOAD_FILENAME=""


# Поиск идеального файла: без debug-info, без glibc-X.Y, tar.gz
log_info "Searching for suitable asset for ${TARGET_OS}-${TARGET_ARCH}..."

# Сначала ищем универсальный (без glibc) и без debug-info, tar.gz
for ASSET in $(echo "$ASSETS"); do
    ASSET_NAME=$(echo "$ASSET" | jq -r .name)
    ASSET_URL=$(echo "$ASSET" | jq -r .browser_download_url)

    if [[ "$ASSET_NAME" == *"${FILENAME_PATTERN}."${TARBALL_EXT} && \
          "$ASSET_NAME" != *"-debug-info."* && \
          "$ASSET_NAME" != *"-glibc-2.27."* && \
          "$ASSET_NAME" != *"-glibc-2.29."* ]]; then # Добавил проверку на glibc-2.29, так как бывает и такой
        DOWNLOAD_URL="$ASSET_URL"
        DOWNLOAD_FILENAME="$ASSET_NAME"
        break
    fi
done

# Если не нашли универсальный, попробуем найти glibc-2.27 или glibc-2.29 вариант для Linux
if [[ -z "$DOWNLOAD_URL" && "$TARGET_OS" == "linux" ]]; then
    # Предпочтем 2.29, потом 2.27
    for GLIBC_VER in "2.29" "2.27"; do
        for ASSET in $(echo "$ASSETS"); do
            ASSET_NAME=$(echo "$ASSET" | jq -r .name)
            ASSET_URL=$(echo "$ASSET" | jq -r .browser_download_url)
            if [[ "$ASSET_NAME" == *"${FILENAME_PATTERN}-glibc-${GLIBC_VER}."${TARBALL_EXT} && \
                  "$ASSET_NAME" != *"-debug-info."* ]]; then
                DOWNLOAD_URL="$ASSET_URL"
                DOWNLOAD_FILENAME="$ASSET_NAME"
                break 2 # Выходим из обоих циклов
            fi
        done
    done
fi

# Если все еще не нашли, попробуем ZIP-архивы как резервный вариант
if [[ -z "$DOWNLOAD_URL" ]]; then
    TARBALL_EXT="zip" # Меняем на zip
    # Снова ищем универсальный (без glibc) и без debug-info, zip
    for ASSET in $(echo "$ASSETS"); do
        ASSET_NAME=$(echo "$ASSET" | jq -r .name)
        ASSET_URL=$(echo "$ASSET" | jq -r .browser_download_url)
        if [[ "$ASSET_NAME" == *"${FILENAME_PATTERN}."${TARBALL_EXT} && \
              "$ASSET_NAME" != *"-debug-info."* && \
              "$ASSET_NAME" != *"-glibc-2.27."* && \
              "$ASSET_NAME" != *"-glibc-2.29."* ]]; then
            DOWNLOAD_URL="$ASSET_URL"
            DOWNLOAD_FILENAME="$ASSET_NAME"
            break
        fi
    done

    # Если не нашли универсальный zip, попробуем найти glibc-2.27 или 2.29 вариант для Linux
    if [[ -z "$DOWNLOAD_URL" && "$TARGET_OS" == "linux" ]]; then
        for GLIBC_VER in "2.29" "2.27"; do
            for ASSET in $(echo "$ASSETS"); do
                ASSET_NAME=$(echo "$ASSET" | jq -r .name)
                ASSET_URL=$(echo "$ASSET" | jq -r .browser_download_url)
                if [[ "$ASSET_NAME" == *"${FILENAME_PATTERN}-glibc-${GLIBC_VER}."${TARBALL_EXT} && \
                      "$ASSET_NAME" != *"-debug-info."* ]]; then
                    DOWNLOAD_URL="$ASSET_URL"
                    DOWNLOAD_FILENAME="$ASSET_NAME"
                    break 2
                fi
            done
        done
    fi
fi


if [[ -z "$DOWNLOAD_URL" ]]; then
    log_error "Could not find a suitable Slang archive for ${TARGET_OS}-${TARGET_ARCH} in version ${LATEST_VERSION}." \
              "You may need to manually download it from GitHub releases page or specify a different RELEASE_TAG."
fi

log_info "Found asset: ${DOWNLOAD_FILENAME}"

# --- Скачивание и распаковка ---
TEMP_DIR=$(mktemp -d -t slang_install_XXXXXXXX)
log_info "Downloading ${DOWNLOAD_FILENAME} to ${TEMP_DIR}..."
curl -L "${DOWNLOAD_URL}" -o "${TEMP_DIR}/${DOWNLOAD_FILENAME}"

if [[ $? -ne 0 ]]; then
    log_error "Failed to download the archive."
fi

log_info "Extracting archive..."
case "${DOWNLOAD_FILENAME}" in
    *.tar.gz)
        tar -xzf "${TEMP_DIR}/${DOWNLOAD_FILENAME}" -C "${TEMP_DIR}"
        ;;
    *.zip)
        check_dependency "unzip"
        unzip -q "${TEMP_DIR}/${DOWNLOAD_FILENAME}" -d "${TEMP_DIR}" # -q для suppress'а вывода
        ;;
    *)
        log_error "Unsupported archive type: ${DOWNLOAD_FILENAME}. Only .tar.gz and .zip are supported."
        ;;
esac

# --- Поиск slangc и его библиотек в распакованной директории ---
SLANGC_EXEC_PATH=""
LIBS_SOURCE_DIR=""

# Slangc и его библиотеки могут быть в разных местах.
# Примерная структура может быть:
# - <TEMP_DIR>/slang-<version>/bin/linux-x64/slangc
# - <TEMP_DIR>/slang-<version>/bin/linux-x64/libslang-compiler.so...
# - <TEMP_DIR>/bin/linux-x64/slangc
# - <TEMP_DIR>/bin/linux-x64/libslang-compiler.so...
# - <TEMP_DIR>/slangc
# - <TEMP_DIR>/libslang-compiler.so...

# Поиск исполняемого файла slangc и определение его родительской директории
# Начинаем поиск с более глубоких путей
POSSIBLE_BASE_PATHS=(
    "${TEMP_DIR}/slang-${LATEST_VERSION}"
    "${TEMP_DIR}"
)

for BASE_PATH in "${POSSIBLE_BASE_PATHS[@]}"; do
    # Поиск slangc
    POTENTIAL_SLANGC=$(find "$BASE_PATH" -type f -name "slangc" -print -quit)
    if [[ -n "$POTENTIAL_SLANGC" ]]; then
        SLANGC_EXEC_PATH="$POTENTIAL_SLANGC"
        # Директория, где нашелся slangc, может содержать и библиотеки
        LIBS_SOURCE_DIR=$(dirname "$SLANGC_EXEC_PATH")
        # Также проверяем стандартную поддиректорию 'lib' относительно этой директории
        if [[ -d "${LIBS_SOURCE_DIR}/lib" ]]; then
            LIBS_SOURCE_DIR="${LIBS_SOURCE_DIR}/lib"
        elif [[ -d "${BASE_PATH}/lib/${TARGET_OS}-${TARGET_ARCH}" ]]; then # Вторая возможная структура
            LIBS_SOURCE_DIR="${BASE_PATH}/lib/${TARGET_OS}-${TARGET_ARCH}"
        elif [[ -d "${BASE_PATH}/lib" ]]; then # Либо просто 'lib'
            LIBS_SOURCE_DIR="${BASE_PATH}/lib"
        fi
        break
    fi
done


if [[ -z "$SLANGC_EXEC_PATH" ]]; then
    log_error "Could not find 'slangc' executable inside the extracted archive. Please check the archive structure or update the script."
fi

log_info "Resolved slangc executable path: ${SLANGC_EXEC_PATH}"
log_info "Likely libraries source directory: ${LIBS_SOURCE_DIR}"

# --- Установка slangc и его библиотек ---
log_info "Installing slangc to: ${INSTALL_BIN_DIR}"
log_info "Installing shared libraries to: ${INSTALL_LIB_DIR}"

# Создаем директории, если их нет
mkdir -p "${INSTALL_BIN_DIR}" || log_error "Failed to create installation directory for binaries: ${INSTALL_BIN_DIR}"
mkdir -p "${INSTALL_LIB_DIR}" || log_error "Failed to create installation directory for libraries: ${INSTALL_LIB_DIR}"


# Проверяем права на запись для INSTALL_BIN_DIR и INSTALL_LIB_DIR
can_write_bin=false
if [[ -w "${INSTALL_BIN_DIR}" ]]; then
    can_write_bin=true
fi

can_write_lib=false
if [[ -w "${INSTALL_LIB_DIR}" ]]; then
    can_write_lib=true
fi


# Функция для выполнения команды с или без sudo
execute_with_sudo_if_needed() {
    local cmd="$1"
    local target_dir="$2"
    local can_write_var="$3" # 'true' или 'false'
    local message="$4"

    if [[ "$can_write_var" == "true" ]]; then
        eval "$cmd" || log_error "$message"
    else
        log_info "Insufficient permissions for ${target_dir}. Attempting '$cmd' with sudo..."
        eval "sudo $cmd" || log_error "$message with sudo."
    fi
}

# Копируем slangc
execute_with_sudo_if_needed "cp \"${SLANGC_EXEC_PATH}\" \"${INSTALL_BIN_DIR}/slangc\"" \
                            "${INSTALL_BIN_DIR}" "$can_write_bin" "Failed to copy slangc"
chmod +x "${INSTALL_BIN_DIR}/slangc" || log_error "Failed to make slangc executable."


# Копируем библиотеки
# macOS не использует ldconfig и обычно не требует централизованного копирования .dylib в /usr/local/lib
# macOS работает с @rpath или путем к dylib относительно исполняемого файла
# Для macOS, если библиотеки лежат рядом со slangc, все должно работать без дополнительных шагов
if [[ "$OS_NAME" == "Linux" ]]; then
    if compgen -G "${LIBS_SOURCE_DIR}/*.so*" > /dev/null; then # Проверяем, есть ли .so файлы
        log_info "Copying shared libraries from ${LIBS_SOURCE_DIR} to ${INSTALL_LIB_DIR}..."
        execute_with_sudo_if_needed "cp -r \"${LIBS_SOURCE_DIR}\"/*.so* \"${INSTALL_LIB_DIR}/\"" \
                                    "${INSTALL_LIB_DIR}" "$can_write_lib" "Failed to copy slangc libraries"
        # Обновляем кеш компоновщика
        execute_with_sudo_if_needed "ldconfig" \
                                    "/" "false" "Failed to update dynamic linker cache (ldconfig)" # ldconfig всегда требует sudo
    else
        log_warn "No shared libraries (.so files) found in ${LIBS_SOURCE_DIR}. slangc might still fail if it has external dependencies."
    fi
elif [[ "$OS_NAME" == "Darwin" ]]; then
    # На macOS, если slangc и его dylib'ы оказываются в одной директории, @rpath обычно работает.
    # Если они в разных, то нужно устанавливать DYLD_LIBRARY_PATH или использовать install_name_tool (сложнее).
    # Предполагаем, что они рядом или в таком месте, где @rpath будет работать.
    if [[ "$LIBS_SOURCE_DIR" != "$(dirname "$SLANGC_EXEC_PATH")" ]]; then
        log_warn "On macOS, slangc and its libraries were found in different locations within the archive."
        log_warn "This might require adjusting DYLD_LIBRARY_PATH or using install_name_tool, which this script does not automatically handle."
    fi
    # Копирование Mac-овских dylib файлов.
    if compgen -G "${LIBS_SOURCE_DIR}/*.dylib" > /dev/null; then # Проверяем, есть ли .dylib файлы
        log_info "Copying shared libraries from ${LIBS_SOURCE_DIR} to ${INSTALL_LIB_DIR}..."
        execute_with_sudo_if_needed "cp -r \"${LIBS_SOURCE_DIR}\"/*.dylib \"${INSTALL_LIB_DIR}/\"" \
                                    "${INSTALL_LIB_DIR}" "$can_write_lib" "Failed to copy slangc libraries"
    else
        log_warn "No shared libraries (.dylib files) found in ${LIBS_SOURCE_DIR}. slangc might still fail if it has external dependencies."
    fi
fi


# --- Проверка установки ---
log_info "Verifying installation..."
if command -v slangc &> /dev/null; then
    log_info "slangc executable available in PATH: $(command -v slangc)"
    # Попытка запустить slangc --version для дополнительной проверки
    if slangc --version &> /dev/null; then
        log_info "slangc appears to be working correctly."
    else
        log_warn "slangc is found in PATH, but failed to run (e.g., 'slangc --version'). This might indicate an issue with dynamic library loading."
        log_warn "If on Linux, ensure '${INSTALL_LIB_DIR}' is in your LD_LIBRARY_PATH or /etc/ld.so.conf.d paths."
        log_warn "If on macOS, ensure '${INSTALL_LIB_DIR}' is in your DYLD_LIBRARY_PATH or DYLD_FALLBACK_LIBRARY_PATH."
    fi
else
    log_warn "slangc executable was copied to '${INSTALL_BIN_DIR}/slangc', but it is not found in your system's PATH."
    log_warn "You may need to add '${INSTALL_BIN_DIR}' to your PATH environment variable."
    if [[ "$INSTALL_BIN_DIR" == "$HOME/.local/bin" ]]; then
        log_info "Most modern Linux distros and macOS add ~/.local/bin to PATH automatically. You might just need to restart your terminal."
    else
        log_info "To add it temporarily: export PATH=\"${INSTALL_BIN_DIR}\":\$PATH"
        log_info "To add it permanently, edit your shell's configuration file (e.g., ~/.bashrc or ~/.zshrc)."
    fi
fi


# --- Очистка ---
log_info "Cleaning up temporary directory: ${TEMP_DIR}"
rm -rf "${TEMP_DIR}"

log_info "Slangc installation process complete!"

