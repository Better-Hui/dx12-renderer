# Copy shared runtime DLLs as a build event on the real demo target.
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/WinPixEventRuntime/bin/WinPixEventRuntime.dll" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/DXC/dxil.dll" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/DXC/dxcompiler.dll" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        VERBATIM
        )

if (DX12_RENDERER_ENABLE_D3D12_AGILITY)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/D3D12" "$<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12"
                VERBATIM
                )
endif()
