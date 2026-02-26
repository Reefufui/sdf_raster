cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

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

include(FetchContent)

message(STATUS "Checking for volk_project...")
FetchContent_Declare(
    volk_project
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(volk_project)
message(STATUS "volk_project is available (SOURCE_DIR: ${volk_project_SOURCE_DIR})")

message(STATUS "Checking for glfw_project...")
FetchContent_Declare(
    glfw_project
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
        -DGLFW_BUILD_DOCS=OFF
        -DGLFW_INSTALL=OFF
)
FetchContent_MakeAvailable(glfw_project)
message(STATUS "glfw_project is available (SOURCE_DIR: ${glfw_project_SOURCE_DIR})")

message(STATUS "Checking for litemath...")
FetchContent_Declare(
    litemath
    GIT_REPOSITORY https://github.com/msu-graphics-group/LiteMath.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(litemath)
message(STATUS "litemath is available (SOURCE_DIR: ${litemath_SOURCE_DIR})")

message(STATUS "Checking for vk_utils_project...")
FetchContent_Declare(
    vk_utils_project
    GIT_REPOSITORY https://github.com/msu-graphics-group/vk-utils.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vk_utils_project)
message(STATUS "vk_utils_project is available (SOURCE_DIR: ${vk_utils_project_SOURCE_DIR})")

message(STATUS "Checking for stb_project...")
FetchContent_Declare(
    stb_project
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb_project) # Загрузит репозиторий и сделает его доступным
message(STATUS "stb_project is available (SOURCE_DIR: ${stb_project_SOURCE_DIR})")

message(STATUS "Checking for nlohmann_json...")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)
message(STATUS "nlohmann_json is available (SOURCE_DIR: ${nlohmann_json_SOURCE_DIR})")

message(STATUS "Checking for imgui...")
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)
message(STATUS "imgui is available (SOURCE_DIR: ${imgui_SOURCE_DIR})")

message(STATUS "Checking for spdlog...")
FetchContent_Declare(
    spdlog_project
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
        -DSPDLOG_BUILD_SHARED=OFF
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_BUILD_TESTS=OFF
        -DSPDLOG_INSTALL=OFF
        -DSPDLOG_COMPILED_LIB_ASYNC=ON
)
FetchContent_MakeAvailable(spdlog_project)
message(STATUS "spdlog_project is available (SOURCE_DIR: ${spdlog_project_SOURCE_DIR})")

add_executable(${PROJECT_NAME}
    ${litemath_SOURCE_DIR}/Image2d.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_alloc_simple.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_buffers.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_context.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_copy.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_descriptor_sets.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_images.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_pipeline.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_swapchain.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_utils.cpp

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp

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

    src/cpu_sandbox/task_generator.cpp
    src/cpu_sandbox/mesh_sphere.cpp
    src/cpu_sandbox/mesh_constructor.cpp
)

if (VULKAN_SDK_REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE "${VULKAN_SDK_ROOT}/include")
    link_directories("${VULKAN_SDK_ROOT}/lib")

    if (NOT EXISTS "${VULKAN_SDK_ROOT}/lib/libvulkan${VULKAN_LIB_EXT}")
        message(FATAL_ERROR "Could not find Vulkan loader library at ${VULKAN_SDK_ROOT}/lib/libvulkan${VULKAN_LIB_EXT}. Please check your Vulkan SDK installation.")
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES
        INSTALL_RPATH "$ENV{VULKAN_SDK}/lib")

endif ()

target_compile_definitions(${PROJECT_NAME} PUBLIC USE_STB_IMAGE)
target_compile_definitions(${PROJECT_NAME} PRIVATE USE_VOLK)
target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${vk_utils_project_SOURCE_DIR}
    ${litemath_SOURCE_DIR}
    ${stb_project_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    volk
    Vulkan::Vulkan
    nlohmann_json
    ${Vulkan_LOADER_LIBRARY}
    spdlog
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

set (SLANGC "")
set (SLANG_COMPILER "" CACHE PATH "Path to the Slang compiler (slangc) executable. Overrides VULKAN_SDK's slangc if set.")
if (SLANG_COMPILER)
    if (EXISTS "${SLANG_COMPILER}" AND IS_EXECUTABLE "${SLANG_COMPILER}")
        set (SLANGC "${SLANG_COMPILER}")
        message (STATUS "Using user-specified slangc: ${SLANGC}")
    else ()
        message( FATAL_ERROR "User-specified Slang compiler '${SLANG_COMPILER}' is not a valid executable or does not exist.")
    endif ()
endif ()

if (NOT SLANGC AND VULKAN_SDK_REQUIRED)
    set (SDK_SLANGC "$ENV{VULKAN_SDK}/bin/slangc")
    if (EXISTS "${SDK_SLANGC}" AND IS_EXECUTABLE "${SDK_SLANGC}")
        set (SLANGC "${SDK_SLANGC}")
        message (STATUS "Using slangc from VULKAN_SDK: ${SLANGC}")
    else ()
        message (WARNING "VULKAN_SDK found (${VULKAN_SDK_ROOT}), but slangc not found at '${SDK_SLANGC}' or is not executable. Attempting to find slangc in system PATH.")
    endif ()
endif ()

if (NOT SLANGC)
    find_program (SLANG_COMPILER_EXECUTABLE slangc)
    if (SLANG_COMPILER_EXECUTABLE)
        set (SLANGC "${SLANG_COMPILER_EXECUTABLE}")
        set (SLANG_COMPILER_EXECUTABLE ${SLANG_COMPILER_EXECUTABLE} CACHE PATH "Path to the Slang compiler (slangc) executable if not using Vulkan SDK's version." FORCE)
        message (STATUS "Found slangc in PATH: ${SLANG_COMPILER_EXECUTABLE}")
    else ()
        message (FATAL_ERROR "slangc not found. Please set the SLANG_COMPILER CMake variable or ensure slangc is in your PATH.")
    endif ()
endif ()

if (SLANGC)
    set(SLANG_SHADERS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
    set(SPIRV_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets/shaders)

    file(GLOB_RECURSE SLANG_SOURCE_FILES
        "${SLANG_SHADERS_SOURCE_DIR}/*.slang"
        )

    list(APPEND BUILT_SPIRV_FILES)

    foreach(SLANG_FILE ${SLANG_SOURCE_FILES})
        get_filename_component(FILE_NAME ${SLANG_FILE} NAME)
        set(OUTPUT_SPV_FILE "${SPIRV_OUTPUT_DIR}/${FILE_NAME}.spv")

        add_custom_command(
            OUTPUT ${OUTPUT_SPV_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_OUTPUT_DIR}/"
            COMMAND ${SLANGC} ${SLANG_FILE} -o ${OUTPUT_SPV_FILE} -target spirv -matrix-layout-column-major -O3
            DEPENDS ${SLANG_FILE}
            VERBATIM
            )
        list(APPEND BUILT_SPIRV_FILES ${OUTPUT_SPV_FILE})
    endforeach()

    add_custom_target(CompileSlangShaders ALL DEPENDS ${BUILT_SPIRV_FILES})

    add_dependencies(${PROJECT_NAME} CompileSlangShaders)
else()
    message(WARNING "Slang compiler not found or specified. Shader compilation targets will be skipped.")
endif()

set(SDF_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/sdf")
set(SDF_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets/sdf")
add_custom_command(
    TARGET ${PROJECT_NAME}
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SDF_SOURCE_DIR}" "${SDF_BINARY_DIR}"
    COMMENT "Copying assets/sdf to binary directory"
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets/)

