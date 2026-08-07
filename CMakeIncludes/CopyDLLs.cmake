# Modify Begin:2026-07-21 by BestHui
# Copy shared runtime DLLs once to avoid parallel post-build copy races.
if (NOT TARGET CopyRuntimeDlls)
        add_custom_target(CopyRuntimeDlls
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/WinPixEventRuntime/bin/WinPixEventRuntime.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/DXC/dxil.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/DXC/dxcompiler.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
                VERBATIM
                )
endif()

add_dependencies(${TARGET_NAME} CopyRuntimeDlls)

# Modify Begin:2026-07-30 by BestHui
if (DX12_RENDERER_ENABLE_D3D12_AGILITY AND NOT TARGET CopyD3D12AgilityRuntime)
        add_custom_target(CopyD3D12AgilityRuntime
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/D3D12"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/D3D12" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/D3D12"
                VERBATIM
                )
endif()

if (DX12_RENDERER_ENABLE_D3D12_AGILITY AND TARGET CopyD3D12AgilityRuntime)
        add_dependencies(${TARGET_NAME} CopyD3D12AgilityRuntime)
endif()
# Modify End
