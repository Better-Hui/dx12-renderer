# Modify Begin:2026-08-16 by BestHui
set(DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY "${CMAKE_BINARY_DIR}/ThirdParty")
set(DX12_RENDERER_NRD_SHADER_INCLUDE_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/NRD/Shaders")

function(dx12_renderer_define_imported_library target_name include_directory)
        add_library(${target_name} SHARED IMPORTED GLOBAL)
        set_target_properties(${target_name} PROPERTIES
                IMPORTED_CONFIGURATIONS "Debug;Release"
                IMPORTED_IMPLIB_DEBUG "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Debug/${target_name}.lib"
                IMPORTED_LOCATION_DEBUG "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Debug/${target_name}.dll"
                IMPORTED_IMPLIB_RELEASE "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Release/${target_name}.lib"
                IMPORTED_LOCATION_RELEASE "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Release/${target_name}.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${include_directory}"
                )
endfunction()

dx12_renderer_define_imported_library(NRI "${CMAKE_SOURCE_DIR}/External/NRI/Include")
dx12_renderer_define_imported_library(NRD "${CMAKE_SOURCE_DIR}/External/NRD/Include")
set_target_properties(NRI PROPERTIES INTERFACE_COMPILE_DEFINITIONS "NRI_STATIC_LIBRARY=0")
set_target_properties(NRD PROPERTIES INTERFACE_COMPILE_DEFINITIONS "NRD_STATIC_LIBRARY=0")

add_library(NRDIntegration INTERFACE IMPORTED GLOBAL)
set_target_properties(NRDIntegration PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/External/NRD/Integration"
        )

function(dx12_renderer_add_third_party_prebuild target_name)
        set(DX12_RENDERER_THIRD_PARTY_TARGET DX12RendererThirdPartyPrebuild)
        if (NOT TARGET ${DX12_RENDERER_THIRD_PARTY_TARGET})
                add_custom_target(${DX12_RENDERER_THIRD_PARTY_TARGET}
                        COMMAND "${CMAKE_COMMAND}" "-DDX12_RENDERER_SOURCE_ROOT=${CMAKE_SOURCE_DIR}" "-DDX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY=${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}" "-DDX12_RENDERER_CONFIGURATION=$<CONFIG>" "-DDX12_RENDERER_GENERATOR=${CMAKE_GENERATOR}" "-DDX12_RENDERER_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}" "-DDX12_RENDERER_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}" "-DDX12_RENDERER_DXC_EXECUTABLE=${DX12_RENDERER_DXC_EXECUTABLE}" "-DDX12_RENDERER_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" -P "${CMAKE_SOURCE_DIR}/CMakeIncludes/BuildThirdParty.cmake"
                        BYPRODUCTS
                        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/$<CONFIG>/NRI.lib"
                        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/$<CONFIG>/NRI.dll"
                        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/$<CONFIG>/NRD.lib"
                        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/$<CONFIG>/NRD.dll"
                        COMMENT "Updating isolated NRI and NRD dependencies"
                        VERBATIM
                        )
        endif()
        add_dependencies(${target_name} ${DX12_RENDERER_THIRD_PARTY_TARGET})
endfunction()
# Modify End
