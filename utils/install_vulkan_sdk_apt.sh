#!/bin/bash

# Этот скрипт устанавливает Vulkan SDK на Kubuntu и настраивает переменные окружения.
# ВНИМАНИЕ: Скрипт будет устанавливать SDK в вашу домашнюю директорию (~/VulkanSDK/).
# Убедитесь, что у вас есть достаточно места на диске.

# --- Шаг 1: Определение последней версии Vulkan SDK для Linux ---
echo "Попытка определить последнюю версию Vulkan SDK для Linux..."
read -p "Пожалуйста, перейдите на https://sdk.lunarg.com/sdk/download/latest/linux/ и скопируйте прямую ссылку на TAR.XZ файл.
Введите прямую ссылку здесь: " SDK_URL

if [ -z "$SDK_URL" ]; then
    echo "Ошибка: Ссылка для загрузки не предоставлена. Выход."
    exit 1
fi

# Извлекаем имя файла из URL
SDK_FILENAME=$(basename "$SDK_URL")

# --- Шаг 2: Создание директории для SDK ---
INSTALL_DIR="$HOME/VulkanSDK/"
mkdir -p "$INSTALL_DIR"

if [ $? -ne 0 ]; then
    echo "Ошибка: Не удалось создать директорию $INSTALL_DIR. Проверьте права доступа."
    exit 1
fi

echo "Директория для установки SDK: $INSTALL_DIR"

# --- Шаг 3: Загрузка Vulkan SDK ---
echo "Загрузка Vulkan SDK ($SDK_FILENAME) в $INSTALL_DIR..."
wget -q --show-progress "$SDK_URL" -P "$INSTALL_DIR"

if [ $? -ne 0 ]; then
    echo "Ошибка: Не удалось загрузить Vulkan SDK с $SDK_URL. Проверьте подключение к интернету или правильность URL."
    rm -f "$INSTALL_DIR/$SDK_FILENAME" # Удаляем частичный файл, если есть
    exit 1
fi

echo "Vulkan SDK успешно загружен."

# --- Шаг 4: Распаковка SDK ---
echo "Распаковка Vulkan SDK..."
# Обратите внимание на расширение .xz
tar -xf "$INSTALL_DIR/$SDK_FILENAME" -C "$INSTALL_DIR"

if [ $? -ne 0 ]; then
    echo "Ошибка: Не удалось распаковать Vulkan SDK. Возможно, файл поврежден или не является tar.xz."
    exit 1
fi

echo "Vulkan SDK успешно распакован."

# --- Шаг 5: Определение пути к VULKAN_SDK ---
# Находим директорию с версией SDK внутри INSTALL_DIR (например, "1.4.328.1")
# Это должен быть единственный каталог, который tar создал напрямую в INSTALL_DIR
SDK_BASE_DIR=$(find "$INSTALL_DIR" -maxdepth 2 -type d -name "x86_64" | xargs dirname | sort | uniq | head -n 1)

if [ -z "$SDK_BASE_DIR" ]; then
    echo "Ошибка: CORE SDK директория (содержащая 'x86_64') не найдена после распаковки."
    echo "Пожалуйста, вручную проверьте содержимое $INSTALL_DIR и установите VULKAN_SDK_PATH."
    exit 1
fi
VULKAN_SDK_PATH="$SDK_BASE_DIR/x86_64"

# Проверка, что каталог x86_64 существует и полученный путь корректен
if [ ! -d "$VULKAN_SDK_PATH" ]; then
    echo "Ошибка: Конечный каталог '$VULKAN_SDK_PATH' для VULKAN_SDK не найден или не является директорией."
    echo "Пожалуйста, вручную проверьте содержимое $INSTALL_DIR"
    exit 1
fi

echo "Определен VULKAN_SDK_PATH: $VULKAN_SDK_PATH"

# --- Шаг 6: Настройка переменных окружения ---
BASHRC_FILE="$HOME/.bashrc"
PROFILE_FILE="$HOME/.profile" # Также обновим на случай, если .bashrc не используется напрямую

echo "Настройка переменных окружения в $BASHRC_FILE и $PROFILE_FILE..."

# Функции для добавления/обновления переменных
add_or_update_env_var() {
    local var_name="$1"
    local var_value="$2"
    local config_file="$3"

    # Удаляем старые записи переменной, если они есть
    # Сначала удаляем блок, чтобы не было дубликатов с комментариями
    sed -i '/# Vulkan SDK environment variables (added by VulkanSDK setup script)/,/# End Vulkan SDK environment variables/d' "$config_file"
    # Теперь удалим отдельные export, если они остались (могли быть до запуска скрипта)
    sed -i "/^export ${var_name}=/d" "$config_file"
    # Удаляем PATH и LD_LIBRARY_PATH, которые могли быть настроены на старый SDK
    sed -i "/export PATH=.*${INSTALL_DIR//\//\\/}.*/d" "$config_file"
    sed -i "/export LD_LIBRARY_PATH=.*${INSTALL_DIR//\//\\/}.*/d" "$config_file"


    # Добавляем новые записи
    echo "" >> "$config_file"
    echo "# Vulkan SDK environment variables (added by VulkanSDK setup script)" >> "$config_file"
    echo "export VULKAN_SDK=\"$var_value\"" >> "$config_file"
    echo "export PATH=\"\$VULKAN_SDK/bin:\$PATH\"" >> "$config_file"
    echo "export LD_LIBRARY_PATH=\"\$VULKAN_SDK/lib:\$LD_LIBRARY_PATH\"" >> "$config_file"
    echo "# End Vulkan SDK environment variables" >> "$config_file"
}

add_or_update_env_var "VULKAN_SDK" "$VULKAN_SDK_PATH" "$BASHRC_FILE"
add_or_update_env_var "VULKAN_SDK" "$VULKAN_SDK_PATH" "$PROFILE_FILE"

echo "Переменные окружения добавлены/обновлены."
echo "Для их активации в текущей сессии выполните: source $BASHRC_FILE"
echo "ИЛИ перезапустите терминал/войдите заново."

# --- Шаг 7: Установка дополнительных зависимостей (если их нет) ---
# УДАЛЕН shaderc-utils, так как он не найден
echo "Установка необходимых пакетов для Vulkan (mesa-vulkan-drivers, libvulkan-dev, spirv-tools)..."
sudo apt update
sudo apt install -y mesa-vulkan-drivers libvulkan-dev spirv-tools

if [ $? -ne 0 ]; then
    echo "Предупреждение: Не удалось установить все дополнительные пакеты. Возможно, они уже установлены или возникли проблемы с APT."
fi

# --- Шаг 8: Очистка ---
echo "Удаление загруженного архива ($SDK_FILENAME)..."
rm -f "$INSTALL_DIR/$SDK_FILENAME"

echo "Очистка завершена."

# --- Шаг 9: Проверка установки ---
echo ""
echo "--- Проверка установки Vulkan SDK ---"
# Теперь мы можем просто вывести значение VULKAN_SDK, которое определили ранее в скрипте.
# Для проверки PATH/LD_LIBRARY_PATH и vulkaninfo требуется запустить source.
echo "VULKAN_SDK, который будет установлен: $VULKAN_SDK_PATH"

# Для полноценной проверки окружения нам нужно временно его активировать
# Если бы мы запускали этот скрипт в новой сессии терминала, все было бы уже активно.
# Но в текущей сессии нужно сделать source.
if [ -f "$BASHRC_FILE" ]; then
    source "$BASHRC_FILE"
    echo "VULKAN_SDK в текущей сессии после source: $VULKAN_SDK"
fi

if command -v vulkaninfo &> /dev/null; then
    echo "Команда 'vulkaninfo' найдена."
    echo "Проверьте вывод 'vulkaninfo' на наличие ошибок - запустите вручную 'vulkaninfo'."
else
    echo "Ошибка: 'vulkaninfo' не найдена. Возможно, переменные окружения не настроены корректно, или SDK установлен не полностью."
fi

echo "--- Установка завершена ---"
echo "Теперь вы можете попробовать собрать ваш CMake проект."
echo "Не забудьте перезапустить терминал или выполнить 'source ~/.bashrc' перед сборкой."
