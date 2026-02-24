#!/bin/bash

log_info() { echo "INFO: $1"; }
log_warn() { echo "WARNING: $1" >&2; }
log_error() { echo "ERROR: $1" >&2; exit 1; }

echo "Starting deep scan for Vulkan SDK installations..."
echo "This might take a while, depending on your system's size and speed."
echo "Press Ctrl+C to abort."

# Общие директории, где часто устанавливают SDK
SEARCH_PATHS=(
    "/usr/local"
    "/opt"
    "$HOME"
    "/usr/share" # Для системных пакетов
)

FOUND_SDK_PATHS=()

# --- Предварительная проверка для ускорения ---
# Проверим известные места, где LunarG SDK обычно себя объявляет
# Ищем директории вида VulkanSDK/X.Y.Z.0
log_info "Searching for known LunarG Vulkan SDK directory patterns..."
for path in "${SEARCH_PATHS[@]}"; do
    # Ищем что-то типа "<path>/VulkanSDK/1.x.y.z/arch"
    find "$path" -maxdepth 4 -type d -name "VulkanSDK" 2>/dev/null | while read sdk_base_path; do
        # Ищем поддиректории с версией
        find "$sdk_base_path" -maxdepth 2 -type d -regextype posix-extended -regex ".*/[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" 2>/dev/null | while read version_path; do
            # Проверяем, есть ли там bin/lib/include
            if [[ -d "$version_path/bin" && -d "$version_path/lib" && -d "$version_path/include" ]]; then
                FOUND_SDK_PATHS+=("$version_path")
            fi
        done
    done
    # Ищем что-то типа "<path>/vulkan/1.x.y.z/arch"
    find "$path" -maxdepth 4 -type d -name "vulkan" 2>/dev/null | while read sdk_base_path; do
        # Ищем поддиректории с версией
        find "$sdk_base_path" -maxdepth 2 -type d -regextype posix-extended -regex ".*/[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" 2>/dev/null | while read version_path; do
            # Проверяем, есть ли там bin/lib/include
            if [[ -d "$version_path/bin" && -d "$version_path/lib" && -d "$version_path/include" ]]; then
                FOUND_SDK_PATHS+=("$version_path")
            fi
        done
    done
done

# Удаляем дубликаты и выводим уникальные пути
FOUND_SDK_PATHS=($(printf "%s\n" "${FOUND_SDK_PATHS[@]}" | sort -u))

if [[ ${#FOUND_SDK_PATHS[@]} -gt 0 ]]; then
    log_info "Likely LunarG Vulkan SDK installations found based on directory structure:"
    for sdk_path in "${FOUND_SDK_PATHS[@]}"; do
        echo "- $sdk_path"
    done
else
    log_warn "No obvious LunarG Vulkan SDK installations found by directory pattern."
fi

# --- Расширенный поиск по файлам ---
# Теперь ищем по наличию ключевых файлов и пытаемся извлечь версию
echo ""
log_info "Performing a deeper scan by searching for key Vulkan SDK files..."

# List of key files to search for
KEY_FILES=(
    "vulkan/vulkan.h"
    "bin/glslc"
    "bin/glslangValidator"
    "bin/slangc"
    "lib/libvulkan.so" # Linux
    "lib/libvulkan.dylib" # macOS
)

# Используем find для поиска директорий, содержащих эти файлы
# Ищем root SDK directory
# Например, директория X/Y/Z, где X/Y/Z/include/vulkan/vulkan.h и X/Y/Z/bin/glslc
CANDIDATE_SDK_ROOTS=()

for path in "${SEARCH_PATHS[@]}"; do
    find "$path" -type f -name "vulkan.h" -path "*/include/vulkan/vulkan.h" 2>/dev/null | while read header_path; do
        # Try to infer SDK root from header_path
        sdk_root=$(dirname "$(dirname "$header_path")") # Goes up from .../include/vulkan/vulkan.h to .../include, then to SDK_ROOT
        
        # Check if this inferred root contains bin/ and lib/
        if [[ -d "$sdk_root/bin" && -d "$sdk_root/lib" ]]; then
            # Verify if it contains some SDK tools
            if [[ -f "$sdk_root/bin/glslc" || -f "$sdk_root/bin/glslangValidator" || -f "$sdk_root/bin/slangc" ]]; then
                CANDIDATE_SDK_ROOTS+=("$sdk_root")
            fi
        fi
    done
done

CANDIDATE_SDK_ROOTS=($(printf "%s\n" "${CANDIDATE_SDK_ROOTS[@]}" | sort -u))

if [[ ${#CANDIDATE_SDK_ROOTS[@]} -eq 0 && ${#FOUND_SDK_PATHS[@]} -eq 0 ]]; then
    log_error "No Vulkan SDK installations could be positively identified on this system."
fi

FINAL_SDK_ANALYZE_PATHS=("${FOUND_SDK_PATHS[@]}" "${CANDIDATE_SDK_ROOTS[@]}")
FINAL_SDK_ANALYZE_PATHS=($(printf "%s\n" "${FINAL_SDK_ANALYZE_PATHS[@]}" | sort -u))

if [[ ${#FINAL_SDK_ANALYZE_PATHS[@]} -gt 0 ]]; then
    echo ""
    log_info "Analyzing potential Vulkan SDK installations:"
    for sdk_path in "${FINAL_SDK_ANALYZE_PATHS[@]}"; do
        echo "--------------------------------------------------------"
        echo "Potential SDK path: $sdk_path"
        
        VULKAN_H_PATH="$sdk_path/include/vulkan/vulkan.h"
        VULKAN_CORE_H_PATH="$sdk_path/include/vulkan/vulkan_core.h"
        
        if [[ -f "$VULKAN_CORE_H_PATH" ]]; then
            # Извлекаем VK_HEADER_VERSION
            HEADER_VERSION=$(grep -E "#define VK_HEADER_VERSION [0-9]+" "$VULKAN_CORE_H_PATH" | head -n 1)
            VK_HEADER_VERSION_RAW=$(echo "$HEADER_VERSION" | sed -E 's/.*#define VK_HEADER_VERSION ([0-9]+).*/\1/')
            
            if [[ -n "$VK_HEADER_VERSION_RAW" ]]; then
                # Расчет Major.Minor.Patch из VK_HEADER_VERSION (см. vulkan_core.h)
                let VK_VERSION_MAJOR=$(( ($VK_HEADER_VERSION_RAW >> 22) & 0x7F ))
                let VK_VERSION_MINOR=$(( ($VK_HEADER_VERSION_RAW >> 12) & 0x3FF ))
                let VK_VERSION_PATCH=$(( $VK_HEADER_VERSION_RAW & 0xFFF ))
                echo "  Vulkan Header Version: ${VK_VERSION_MAJOR}.${VK_VERSION_MINOR}.${VK_VERSION_PATCH}"
            else
                echo "  Vulkan Header Version: Could not parse from vulkan_core.h"
            fi
        elif [[ -f "$VULKAN_H_PATH" ]]; then
             # Если vulkan_core.h нет (очень старые SDK), используем только vulkan.h и не пытаемся парсить детально
            echo "  Vulkan Header Version: (vulkan.h found, vulkan_core.h not present or unparseable for detailed version)"
        else
            echo "  Vulkan Header Version: (vulkan.h or vulkan_core.h not found in include/vulkan)"
        fi
        
        echo "  Components found:"
        local found_component=false
        for file in "${KEY_FILES[@]}"; do
            if [[ -f "$sdk_path/$file" ]]; then
                echo "    - $file (Found)"
                found_component=true
            fi
        done
        if ! $found_component; then
            echo "    (No core SDK utilities found, might be a partial or runtime-only installation wrapped as SDK)"
        fi
        echo "--------------------------------------------------------"
    done
else
    log_warn "No Vulkan SDK installations were identified on this system."
fi

log_info "Vulkan SDK scan complete."

