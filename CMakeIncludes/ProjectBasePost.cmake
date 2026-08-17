target_include_directories(${TARGET_NAME}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )

# Modify Begin:2026-08-16 by BestHui
if (DX12_RENDERER_HAS_SHADER_ASSETS)
    set(DX12_RENDERER_SHADER_ASSET_TARGET "${TARGET_NAME}ShaderAssets")
    if (NOT TARGET ${DX12_RENDERER_SHADER_ASSET_TARGET})
        add_custom_target(${DX12_RENDERER_SHADER_ASSET_TARGET}
                COMMAND ${CMAKE_COMMAND} -P "${DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT}"
                COMMENT "Compiling ${TARGET_NAME} shader assets"
                VERBATIM)
    endif()
    add_dependencies(${TARGET_NAME} ${DX12_RENDERER_SHADER_ASSET_TARGET})
endif()
# Modify End

# Modify Begin:2026-07-21 by BestHui
target_include_directories(${TARGET_NAME}
        PUBLIC "${CMAKE_SOURCE_DIR}/Shaders"
        )
# Modify End

if (${SHADERS_OUTPUT_HEADERS}) 
    # add shader headers to include directories
    target_include_directories(${TARGET_NAME}
            PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/Shaders/"
            )
else ()
    # Copy Shaders
    if (NOT ("${SHADER_FILES}" STREQUAL ""))
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_BINARY_DIR}/Shaders
                $<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders)
    endif ()
endif ()

if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
endif ()
