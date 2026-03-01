cmake_minimum_required (VERSION 3.21 FATAL_ERROR)

project (sdf_raster LANGUAGES CXX)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

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

message (STATUS "Checking for system glfw3...")
set (REQUIRED_GLFW_VERSION "3.4.0")
find_package (glfw3 CONFIG REQUIRED)
if (glfw3_VERSION VERSION_GREATER REQUIRED_GLFW_VERSION)
    message (WARNING 
        "GLFW version ${glfw3_VERSION} > required ${REQUIRED_GLFW_VERSION}. "
        "Tested with ${REQUIRED_GLFW_VERSION} only. Possible compatibility issues.")
elseif (glfw3_VERSION VERSION_LESS REQUIRED_GLFW_VERSION)
    message (FATAL_ERROR 
        "GLFW version ${glfw3_VERSION} < required ${REQUIRED_GLFW_VERSION}")
else ()
    message (STATUS "GLFW version ${glfw3_VERSION} - perfect match!")
endif ()
add_library (glfw::glfw ALIAS glfw)

add_subdirectory (submodules/volk)

set (SPDLOG_BUILD_SHARED OFF CACHE BOOL "")
set (SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "")
set (SPDLOG_BUILD_TESTS OFF CACHE BOOL "")
set (SPDLOG_INSTALL OFF CACHE BOOL "")
set (SPDLOG_COMPILED_LIB_ASYNC ON CACHE BOOL "")
add_subdirectory (submodules/spdlog)

set (JSON_BuildTests OFF CACHE BOOL "")
set (JSON_Install OFF CACHE BOOL "")
set (JSON_MultipleHeaders ON CACHE BOOL "")
set (JSON_GlobalUDLs OFF CACHE BOOL "")
set (JSON_ImplicitConversions OFF CACHE BOOL "")
set (JSON_Diagnostics OFF CACHE BOOL "")
set (JSON_Diagnostic_Positions OFF CACHE BOOL "")
add_subdirectory (submodules/json)

add_library (imgui STATIC
    submodules/imgui/imgui.cpp
    submodules/imgui/imgui_demo.cpp
    submodules/imgui/imgui_draw.cpp
    submodules/imgui/imgui_tables.cpp
    submodules/imgui/imgui_widgets.cpp

    submodules/imgui/backends/imgui_impl_glfw.cpp
    submodules/imgui/backends/imgui_impl_vulkan.cpp

    submodules/imgui/misc/freetype/imgui_freetype.cpp
)
target_include_directories (imgui PUBLIC
    submodules/imgui
    submodules/imgui/backends
    submodules/imgui-filebrowser
    submodules/volk
)
target_link_libraries (imgui PRIVATE glfw::glfw)
target_compile_definitions (imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK=1 IMGUI_ENABLE_FREETYPE=1)
add_library (imgui::imgui ALIAS imgui)

find_package (Freetype QUIET)
if (Freetype_FOUND)
    target_link_libraries (imgui PRIVATE Freetype::Freetype)
    target_compile_definitions (imgui PRIVATE IMGUI_FREETYPE)
    message (STATUS "FreeType was found and will be used for ImGUI font rendering.")
else ()
    message (STATUS "FreeType was not found and won't be used for ImGUI font rendering.")
endif ()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options (imgui PRIVATE -g -O0 -Wall -Wextra)
    target_link_options (imgui PRIVATE -g)
elseif (CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options (imgui PRIVATE -O3 -DNDEBUG -Wall -Wextra)
    target_link_options (imgui PRIVATE -O3)
elseif (CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    target_compile_options (imgui PRIVATE -O2 -g -DNDEBUG -Wall -Wextra)
    target_link_options (imgui PRIVATE -O2 -g)
endif ()

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

add_library (LiteMath INTERFACE)
target_include_directories (LiteMath INTERFACE
    submodules/LiteMath
    submodules/LiteMath/external
)
add_library (LiteMath::LiteMath ALIAS LiteMath)
add_library (LiteImage STATIC submodules/LiteMath/Image2d.cpp)
target_compile_definitions (LiteImage PRIVATE USE_STB_IMAGE=0)
target_link_libraries (LiteImage PUBLIC LiteMath)
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

target_include_directories (${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
)

target_link_libraries (${PROJECT_NAME} PRIVATE
    glfw::glfw
    imgui::imgui
    LiteMath::LiteImage
    LiteMath::LiteMath
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    vk_utils::vk_utils
    volk::volk
    Vulkan::Vulkan
)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    message (STATUS "Configuring for Debug build...")
    target_compile_options (${PROJECT_NAME} PRIVATE -g -O0 -Wall -Wextra)
    target_link_options (${PROJECT_NAME} PRIVATE -g)
    target_compile_definitions (${PROJECT_NAME} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE VULKAN_VALIDATION_LAYERS=1)
elseif (CMAKE_BUILD_TYPE MATCHES Release)
    message (STATUS "Configuring for Release build...")
    target_compile_options (${PROJECT_NAME} PRIVATE -O3 -DNDEBUG -Wall -Wextra)
    target_link_options (${PROJECT_NAME} PRIVATE -O3)
    target_compile_definitions (${PROJECT_NAME} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
elseif (CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    message (STATUS "Configuring for Release with Debug Info build...")
    target_compile_options (${PROJECT_NAME} PRIVATE -O2 -g -DNDEBUG -Wall -Wextra)
    target_link_options (${PROJECT_NAME} PRIVATE -O2 -g)
    target_compile_definitions (${PROJECT_NAME} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO VULKAN_VALIDATION_LAYERS=1)
endif ()

add_subdirectory (shaders)
add_subdirectory (assets)

