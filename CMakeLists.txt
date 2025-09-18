cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(TriangleRasterizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-copy")

if (NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK environment variable not set. Please set it before running cmake (e.g., source /etc/profile.d/vulkan_sdk.sh).")
endif()

if (NOT EXISTS "$ENV{VULKAN_SDK}")
    message(FATAL_ERROR "VULKAN_SDK path does not exist: $ENV{VULKAN_SDK}. Please check your environment variable.")
endif()

if (APPLE)
    set(VULKAN_LIB_EXT ".dylib")
elseif (WIN32)
    set(VULKAN_LIB_EXT ".dll")
else()
    set(VULKAN_LIB_EXT ".so")
endif()

set(Vulkan_FOUND TRUE)
set(VULKAN_SDK_ROOT "$ENV{VULKAN_SDK}")

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

message(STATUS "Vulkan found: ${Vulkan_FOUND}")
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")

add_executable(${PROJECT_NAME}
    ${litemath_SOURCE_DIR}/Image2d.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_alloc_simple.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_buffers.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_context.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_copy.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_images.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_utils.cpp
    src/application.cpp
    src/camera.cpp
    src/main.cpp
    src/marching_cubes.cpp
    src/mesh.cpp
    src/sdf_grid.cpp
    src/sdf_octree.cpp
    src/vulkan_context.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE "${VULKAN_SDK_ROOT}/include")

link_directories("${VULKAN_SDK_ROOT}/lib")

if (NOT EXISTS "${VULKAN_SDK_ROOT}/lib/libvulkan${VULKAN_LIB_EXT}")
     message(FATAL_ERROR "Could not find Vulkan loader library at ${VULKAN_SDK_ROOT}/lib/libvulkan${VULKAN_LIB_EXT}. Please check your Vulkan SDK installation.")
endif()

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

target_compile_options(${PROJECT_NAME} PRIVATE -fopenmp)
target_link_options(${PROJECT_NAME} PRIVATE -fopenmp)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Configuring for Debug build...")
    # Попробуйте ТОЛЬКО -g и -O0
    target_compile_options(${PROJECT_NAME} PRIVATE -g -O0 -Wall -Wextra)
    target_link_options(${PROJECT_NAME} PRIVATE -g)
elseif(CMAKE_BUILD_TYPE MATCHES Release)
    message(STATUS "Configuring for Release build...")
    target_compile_options(${PROJECT_NAME} PRIVATE -O3 -DNDEBUG -Wall -Wextra)
    target_link_options(${PROJECT_NAME} PRIVATE -O3)
elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    message(STATUS "Configuring for Release with Debug Info build...")
    target_compile_options(${PROJECT_NAME} PRIVATE -O2 -g -DNDEBUG -Wall -Wextra)
    target_link_options(${PROJECT_NAME} PRIVATE -O2 -g)
endif()

