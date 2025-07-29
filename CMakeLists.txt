cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(TriangleRasterizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(VULKAN_SDK "$ENV{VULKAN_SDK}")

# Проверяем, была ли переменная VULKAN_SDK установлена в окружении CMake
if (NOT VULKAN_SDK)
    message(FATAL_ERROR "VULKAN_SDK environment variable not set. Please set it before running cmake (e.g., source <path_to_sdk>/setup-env.sh).")
endif()

# Убеждаемся, что директория lib существует внутри VULKAN_SDK
if (NOT EXISTS "${VULKAN_SDK}/lib")
    message(FATAL_ERROR "Could not find ${VULKAN_SDK}/lib. Please check your VULKAN_SDK path.")
endif()

# Убеждаемся, что файл libvulkan.dylib существует по ожидаемому пути
if (NOT EXISTS "${VULKAN_SDK}/lib/libvulkan.dylib")
    message(FATAL_ERROR "Could not find Vulkan loader library at ${VULKAN_SDK}/lib/libvulkan.dylib. Please check your Vulkan SDK installation.")
endif()

set(Vulkan_INCLUDE_DIR "${VULKAN_SDK}/include")
set(Vulkan_LOADER_LIBRARY "${VULKAN_SDK}/lib/libvulkan.dylib") # Теперь VULKAN_SDK точно не пустой

if (APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/llvm" "/usr/local")
    list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
    message(STATUS "CMAKE_PREFIX_PATH for macOS set to: ${CMAKE_PREFIX_PATH}")
endif()

include(FetchContent)

FetchContent_Declare(
    volk_project
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(volk_project)

FetchContent_Declare(
    glfw_project
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
)
FetchContent_MakeAvailable(glfw_project)

FetchContent_Declare(
    litemath
    GIT_REPOSITORY https://github.com/msu-graphics-group/LiteMath.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(litemath)

FetchContent_Declare(
    vk_utils_project
    GIT_REPOSITORY https://github.com/msu-graphics-group/vk-utils.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(vk_utils_project)

FetchContent_Declare(
    stb_project
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master # Или конкретный релиз/коммит, например, v1.23
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb_project) # Загрузит репозиторий и сделает его доступным

find_package(Vulkan REQUIRED)
message(STATUS "Vulkan found: ${Vulkan_FOUND}")
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")

find_package(OpenMP REQUIRED)
if (OpenMP_FOUND)
    message(STATUS "OpenMP found. Adding OpenMP flags.")
else()
    message(FATAL_ERROR "OpenMP not found! Please install libomp (brew install libomp) or gcc (brew install gcc).")
endif()

message(STATUS "Vulkan found: ${Vulkan_FOUND}")
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")

add_executable(${PROJECT_NAME}
    src/application.cpp
    src/camera.cpp
    src/cpu_renderer.cpp
    src/main.cpp
    src/mesh.cpp
    src/sdf_grid.cpp
    src/vulkan_context.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_buffers.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_context.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_utils.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_alloc_simple.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_images.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_copy.cpp
    ${litemath_SOURCE_DIR}/Image2d.cpp
)

target_compile_definitions(${PROJECT_NAME} PUBLIC USE_STB_IMAGE)
target_compile_definitions(${PROJECT_NAME} PRIVATE USE_VOLK)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${vk_utils_project_SOURCE_DIR}
    ${litemath_SOURCE_DIR}
    ${stb_project_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    volk
    Vulkan::Vulkan
)

if (OpenMP_FOUND)
    target_compile_options(${PROJECT_NAME} PUBLIC ${OpenMP_CXX_FLAGS})
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
endif()

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders"
    COMMENT "Creating assets/shaders directory"
)
file(GLOB_RECURSE SHADER_SPV_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.spv")
foreach(SHADER_FILE ${SHADER_SPV_FILES})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${SHADER_FILE}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders/${SHADER_NAME}"
        COMMENT "Copying shader: ${SHADER_NAME}"
    )
endforeach()

# Линкуем всё
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${Vulkan_LOADER_LIBRARY} # <-- Линкуемся со стандартным Loader'ом
)

# Установка RPATH на runtime
set_target_properties(${PROJECT_NAME} PROPERTIES
    # Устанавливаем RPATH на директорию, содержащую libvulkan.dylib
    # (Это та же директория, что и для libMoltenVK.dylib в твоем случае)
    INSTALL_RPATH "${VULKAN_SDK}/lib" # <-- Указываем директорию
)

