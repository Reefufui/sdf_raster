# shaders/CMakeLists.txt

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
        set (SLANG_COMPILER_EXECUTABLE ${SLANGC} CACHE PATH "Path to the Slang compiler (slangc) executable if not using Vulkan SDK's version." FORCE)
        message (STATUS "Found slangc in PATH: ${SLANG_COMPILER_EXECUTABLE}")
    else ()
        message (FATAL_ERROR "slangc not found. Please set the SLANG_COMPILER CMake variable or ensure slangc is in your PATH.")
    endif ()
endif ()

if (SLANGC)
    set (SLANG_SHADERS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set (SPIRV_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
    file (MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

    set (SLANG_INCLUDE_OPTIONS "")
    get_filename_component (PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
    set (GLOBAL_INCLUDE_DIR "${PROJECT_ROOT_DIR}/include")
    if (EXISTS "${GLOBAL_INCLUDE_DIR}")
        list (APPEND SLANG_INCLUDE_OPTIONS "-I" "${GLOBAL_INCLUDE_DIR}")
    endif ()
    if (EXISTS "${SLANG_SHADERS_SOURCE_DIR}")
        list (APPEND SLANG_INCLUDE_OPTIONS "-I" "${SLANG_SHADERS_SOURCE_DIR}")
    endif ()

    set (SLANGC_COMMON_ARGS -target spirv -matrix-layout-column-major -O3 ${SLANG_INCLUDE_OPTIONS})

    file (GLOB_RECURSE SLANG_SOURCE_FILES
        "${SLANG_SHADERS_SOURCE_DIR}/*.vert.slang"
        "${SLANG_SHADERS_SOURCE_DIR}/*.frag.slang"
        "${SLANG_SHADERS_SOURCE_DIR}/*.comp.slang"
        "${SLANG_SHADERS_SOURCE_DIR}/*.mesh.slang"
        "${SLANG_SHADERS_SOURCE_DIR}/*.task.slang"
    )

    if (NOT SLANG_SOURCE_FILES)
        message (WARNING "No .slang shader files found for compilation in ${SLANG_SHADERS_SOURCE_DIR}.")
    endif ()

    set (COMPILED_SPV_FILES "")

    foreach (SLANG_FILE ${SLANG_SOURCE_FILES})
        get_filename_component (FULL_NAME ${SLANG_FILE} NAME)

        set (OUTPUT_SPV_FILE "${SPIRV_OUTPUT_DIR}/${FULL_NAME}.spv")
        set (DEP_FILE "${CMAKE_CURRENT_BINARY_DIR}/.slang_deps/${FULL_NAME}.d")

        file (RELATIVE_PATH CLEAN_SPV_PATH ${CMAKE_BINARY_DIR} ${OUTPUT_SPV_FILE})
        file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.slang_deps")

        add_custom_command (
            OUTPUT ${OUTPUT_SPV_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SPIRV_OUTPUT_DIR}
            COMMAND ${SLANGC} ${SLANG_FILE} -o ${OUTPUT_SPV_FILE} -depfile ${DEP_FILE} ${SLANGC_COMMON_ARGS}
            DEPENDS ${SLANG_FILE}
            DEPFILE ${DEP_FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Building SPIR-V object ${CLEAN_SPV_PATH}"
            VERBATIM
        )
        set_property (SOURCE ${OUTPUT_SPV_FILE} PROPERTY GENERATED TRUE)

        list (APPEND COMPILED_SPV_FILES ${OUTPUT_SPV_FILE})
    endforeach ()

    add_custom_target (shaders ALL DEPENDS ${COMPILED_SPV_FILES})
else ()
    message (WARNING "Slang compiler not found or specified. Shader compilation targets will be skipped.")
    add_custom_target (shaders) # NOTE: dummy target
endif ()

