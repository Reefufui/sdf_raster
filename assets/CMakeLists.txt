# assets/CMakeLists.txt

set (ASSETS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set (ASSETS_DESTINATION_DIR "${CMAKE_CURRENT_BINARY_DIR}")

file (RELATIVE_PATH CLEAN_ASSETS_PATH ${CMAKE_BINARY_DIR} ${ASSETS_DESTINATION_DIR})

add_custom_target (
    copy_assets
    ALL
    COMMENT "Copying assets to ${CLEAN_ASSETS_PATH}"
)

add_custom_command (
    TARGET copy_assets
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_SOURCE_DIR}" "${ASSETS_DESTINATION_DIR}"
    VERBATIM
)

