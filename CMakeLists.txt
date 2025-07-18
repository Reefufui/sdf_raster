cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(TriangleRasterizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_CXX_COMPILER)
    find_program(CMAKE_CXX_COMPILER g++)
    if(NOT CMAKE_CXX_COMPILER)
        message(WARNING "g++ compiler not found. CMake will try to use default C++ compiler.")
    endif()
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


find_package(Vulkan REQUIRED)
message(STATUS "Vulkan found: ${Vulkan_FOUND}")
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/application.cpp
    src/camera.cpp
    src/cpu_renderer.cpp
    src/mesh.cpp
    src/vulkan_context.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_buffers.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_context.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_utils.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_alloc_simple.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_images.cpp
    ${vk_utils_project_SOURCE_DIR}/vk_copy.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE USE_VOLK)
# target_compile_definitions(${PROJECT_NAME} PRIVATE GLFW_INCLUDE_VULKAN)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${vk_utils_project_SOURCE_DIR}
    ${litemath_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    volk
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

