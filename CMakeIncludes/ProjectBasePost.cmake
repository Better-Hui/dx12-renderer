target_include_directories(${TARGET_NAME}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )

# Modify Begin:2026-07-30 by BestHui
if (DX12_RENDERER_GENERATED_SHADER_OUTPUTS)
    set_source_files_properties(
            ${DX12_RENDERER_GENERATED_SHADER_OUTPUTS}
            PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
    source_group("Generated\\ShaderBytecode" FILES ${DX12_RENDERER_GENERATED_SHADER_OUTPUTS})
    target_sources(${TARGET_NAME} PRIVATE ${DX12_RENDERER_GENERATED_SHADER_OUTPUTS})
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
