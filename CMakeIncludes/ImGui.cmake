set(DX12_RENDERER_IMGUI_SOURCE_DIRECTORY "${CMAKE_SOURCE_DIR}/External/ImGui")

if (NOT EXISTS "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui.h")
        message(FATAL_ERROR
                "Dear ImGui is missing. Run: git submodule update --init --recursive")
endif ()

set(DX12_RENDERER_IMGUI_COMPILED_SOURCES
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui_draw.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui_tables.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui_widgets.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/backends/imgui_impl_dx12.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/backends/imgui_impl_win32.cpp"
        )
