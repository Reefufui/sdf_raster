cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(sdf_raster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if (CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    set(VULKAN_SDK_REQUIRED TRUE)
else()
    set(VULKAN_SDK_REQUIRED FALSE)
endif()

if (VULKAN_SDK_REQUIRED)
    if (NOT DEFINED ENV{VULKAN_SDK})
        message(FATAL_ERROR "VULKAN_SDK environment variable not set. Please set it before running cmake (e.g., source /etc/profile.d/vulkan_sdk.sh).")
    endif()

    if (NOT EXISTS "$ENV{VULKAN_SDK}")
        message(FATAL_ERROR "VULKAN_SDK path does not exist: $ENV{VULKAN_SDK}. Please check your environment variable.")
    endif()

    set(VULKAN_SDK_ROOT "$ENV{VULKAN_SDK}")
else()
    message(STATUS "Vulkan SDK is not required for this build configuration (${CMAKE_BUILD_TYPE}).")
endif()

if (APPLE)
    set(VULKAN_LIB_EXT ".dylib")
elseif (WIN32)
    set(VULKAN_LIB_EXT ".dll")
else()
    set(VULKAN_LIB_EXT ".so")
endif()

find_package(Vulkan REQUIRED)
message(STATUS "Vulkan ${Vulkan_VERSION} found: ${Vulkan_FOUND}")
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")

set (REQUIRED_VULKAN_MAJOR 1)
set (REQUIRED_VULKAN_MINOR 4)
set (REQUIRED_VULKAN_VERSION_STRING "${REQUIRED_VULKAN_MAJOR}.${REQUIRED_VULKAN_MINOR}")

if (NOT DEFINED Vulkan_VERSION OR "${Vulkan_VERSION}" STREQUAL "")
    message (FATAL_ERROR "Could not determine Vulkan API version from found Vulkan installation. 'Vulkan_VERSION' variable is not set by find_package (Vulkan).")
endif ()

string (REGEX MATCH "^([0-9]+)\\.([0-9]+)" _match_result "${Vulkan_VERSION}")
if (NOT _match_result)
    message (FATAL_ERROR "Could not parse major.minor version from Vulkan_VERSION string '${Vulkan_VERSION}'. Expected format like 'X.Y.Z'.")
endif ()

set (FOUND_VULKAN_MAJOR "${CMAKE_MATCH_1}")
set (FOUND_VULKAN_MINOR "${CMAKE_MATCH_2}")

message (STATUS "Parsed Vulkan API version from string: ${FOUND_VULKAN_MAJOR}.${FOUND_VULKAN_MINOR}")

if (FOUND_VULKAN_MAJOR LESS REQUIRED_VULKAN_MAJOR)
    message (FATAL_ERROR "Found Vulkan API version ${FOUND_VULKAN_MAJOR}.${FOUND_VULKAN_MINOR} is below the required minimum version ${REQUIRED_VULKAN_VERSION_STRING}. Please update your Vulkan drivers/SDK.")
elseif (FOUND_VULKAN_MAJOR EQUAL REQUIRED_VULKAN_MAJOR AND FOUND_VULKAN_MINOR LESS REQUIRED_VULKAN_MINOR)
    message(FATAL_ERROR "Found Vulkan API version ${FOUND_VULKAN_MAJOR}.${FOUND_VULKAN_MINOR} is below the required minimum version ${REQUIRED_VULKAN_VERSION_STRING}. Please update your Vulkan drivers/SDK.")
else ()
    message(STATUS "Found Vulkan API version ${FOUND_VULKAN_MAJOR}.${FOUND_VULKAN_MINOR} meets or exceeds the required minimum version ${REQUIRED_VULKAN_VERSION_STRING}.")
endif ()

message(STATUS "Checking for system glfw3...")
set (REQUIRED_GLFW_VERSION "3.4.0")
find_package(glfw3 CONFIG REQUIRED)
if(glfw3_VERSION VERSION_GREATER REQUIRED_GLFW_VERSION)
    message(WARNING 
        "GLFW version ${glfw3_VERSION} > required ${REQUIRED_GLFW_VERSION}. "
        "Tested with ${REQUIRED_GLFW_VERSION} only. Possible compatibility issues.")
elseif(glfw3_VERSION VERSION_LESS REQUIRED_GLFW_VERSION)
    message(FATAL_ERROR 
        "GLFW version ${glfw3_VERSION} < required ${REQUIRED_GLFW_VERSION}")
else()
    message(STATUS "GLFW version ${glfw3_VERSION} - perfect match!")
endif()
add_library (glfw::glfw ALIAS glfw)

add_subdirectory (submodules/volk)

set (SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set (SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set (SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set (SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set (SPDLOG_COMPILED_LIB_ASYNC ON CACHE BOOL "" FORCE)
add_subdirectory (submodules/spdlog)

set (JSON_BuildTests OFF CACHE BOOL "" FORCE)
set (JSON_Install OFF CACHE BOOL "" FORCE)
set (JSON_MultipleHeaders ON CACHE BOOL "" FORCE)
set (JSON_GlobalUDLs OFF CACHE BOOL "" FORCE)
set (JSON_ImplicitConversions OFF CACHE BOOL "" FORCE)
set (JSON_Diagnostics OFF CACHE BOOL "" FORCE)
set (JSON_Diagnostic_Positions OFF CACHE BOOL "" FORCE)
add_subdirectory (submodules/json)

add_library (imgui STATIC
    submodules/imgui/imgui.cpp
    submodules/imgui/imgui_demo.cpp
    submodules/imgui/imgui_draw.cpp
    submodules/imgui/imgui_tables.cpp
    submodules/imgui/imgui_widgets.cpp
)
target_include_directories (imgui PUBLIC
    submodules/imgui
    submodules/imgui/backends
)
target_compile_features (imgui PRIVATE cxx_std_20)
add_library (imgui::imgui ALIAS imgui)

add_library (vk_utils STATIC
    submodules/vk-utils/vk_alloc_simple.cpp
    submodules/vk-utils/vk_buffers.cpp
    submodules/vk-utils/vk_context.cpp
    submodules/vk-utils/vk_copy.cpp
    submodules/vk-utils/vk_descriptor_sets.cpp
    submodules/vk-utils/vk_images.cpp
    submodules/vk-utils/vk_pipeline.cpp
    submodules/vk-utils/vk_swapchain.cpp
    submodules/vk-utils/vk_utils.cpp
)
target_include_directories (vk_utils PUBLIC submodules/vk-utils)
target_compile_definitions (vk_utils PUBLIC USE_VOLK=1)
target_link_libraries (vk_utils PUBLIC volk)
add_library (vk_utils::vk_utils ALIAS vk_utils)

add_library (stb_image STATIC third_party/stb/stb_image.c)
target_include_directories (stb_image PUBLIC third_party/stb)
add_library (stb::image ALIAS stb_image)

add_library (stb_image_write STATIC third_party/stb/stb_image_write.c)
target_include_directories (stb_image_write PUBLIC third_party/stb)
add_library (stb::image_write ALIAS stb_image_write)

add_library (LiteMath INTERFACE)
target_include_directories (LiteMath INTERFACE
    submodules/LiteMath
    submodules/LiteMath/external
)
add_library (LiteMath::LiteMath ALIAS LiteMath)
add_library (LiteImage STATIC submodules/LiteMath/Image2d.cpp)
target_compile_definitions (LiteImage PRIVATE
    USE_STB_IMAGE=1
    STB_IMAGE_STATIC=1
    STB_IMAGE_WRITE_STATIC=1
)
target_link_libraries (LiteImage PUBLIC LiteMath stb::image_write stb::image)
add_library (LiteMath::LiteImage ALIAS LiteImage)

add_executable(${PROJECT_NAME}
    src/application.cpp
    src/camera.cpp
    src/compute_shader_renderer.cpp
    src/gui.cpp
    src/logger.cpp
    src/main.cpp
    src/marching_cubes_lookup_table.cpp
    src/marching_cubes.cpp
    # DEPRECATED: src/mesh_shader_renderer.cpp
    src/mesh.cpp
    src/occlusion_culling.cpp
    src/sdf_octree.cpp
    src/vulkan_context.cpp
)

if (VULKAN_SDK_REQUIRED)
    target_include_directories (${PROJECT_NAME} PRIVATE "${VULKAN_SDK_ROOT}/include")
    link_directories ("${VULKAN_SDK_ROOT}/lib")

    if (NOT EXISTS "${VULKAN_SDK_ROOT}/lib/libvulkan${VULKAN_LIB_EXT}")
        message (FATAL_ERROR "Could not find Vulkan loader library at ${VULKAN_SDK_ROOT}/lib/libvulkan${VULKAN_LIB_EXT}. Please check your Vulkan SDK installation.")
    endif()

    set_target_properties (${PROJECT_NAME} PROPERTIES INSTALL_RPATH "$ENV{VULKAN_SDK}/lib")
endif ()

target_compile_definitions (${PROJECT_NAME} PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)

target_include_directories (${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${vk_utils_project_SOURCE_DIR}
    ${litemath_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries (${PROJECT_NAME} PRIVATE
    # ${Vulkan_LOADER_LIBRARY}
    glfw::glfw
    imgui::imgui
    LiteMath::LiteImage
    LiteMath::LiteMath
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    vk_utils::vk_utils
    volk::volk
    Vulkan::Vulkan
    stb::image_write
    stb::image
)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Configuring for Debug build...")
    target_compile_options(${PROJECT_NAME} PRIVATE -g -O0 -Wall -Wextra)
    target_link_options(${PROJECT_NAME} PRIVATE -g)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE VULKAN_VALIDATION_LAYERS=1)
elseif(CMAKE_BUILD_TYPE MATCHES Release)
    message(STATUS "Configuring for Release build...")
    target_compile_options(${PROJECT_NAME} PRIVATE -O3 -DNDEBUG -Wall -Wextra)
    target_link_options(${PROJECT_NAME} PRIVATE -O3)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    message(STATUS "Configuring for Release with Debug Info build...")
    target_compile_options(${PROJECT_NAME} PRIVATE -O2 -g -DNDEBUG -Wall -Wextra)
    target_link_options(${PROJECT_NAME} PRIVATE -O2 -g)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO VULKAN_VALIDATION_LAYERS=1)
endif()

target_compile_options(${PROJECT_NAME} PRIVATE -Wno-deprecated-copy)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

add_subdirectory (shaders)
target_include_directories (${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/)

add_subdirectory (assets)
target_include_directories (${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets/)

