# Modify Begin:2026-08-16 by Hui
cmake_minimum_required(VERSION 3.30)

# Modify Begin:2026-08-17 by Hui
# Keep standalone third-party targets on the C++17 baseline required by NRI.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Modify End

if (NOT DEFINED DX12_RENDERER_SOURCE_ROOT OR NOT EXISTS "${DX12_RENDERER_SOURCE_ROOT}/External/NRI/CMakeLists.txt")
        message(FATAL_ERROR "DX12_RENDERER_SOURCE_ROOT must identify a checkout containing External/NRI and External/NRD.")
endif()
if (NOT DEFINED DX12_RENDERER_DXC_EXECUTABLE OR NOT EXISTS "${DX12_RENDERER_DXC_EXECUTABLE}")
        message(FATAL_ERROR "DX12_RENDERER_DXC_EXECUTABLE must identify the renderer DXC executable.")
endif()

project(DX12RendererThirdParty LANGUAGES CXX)

set(DX12_RENDERER_THIRD_PARTY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_OUTPUT_DIRECTORY}")

set(NRI_STATIC_LIBRARY OFF CACHE BOOL "Build NRI as a shared library." FORCE)
set(NRI_ENABLE_NVTX_SUPPORT OFF CACHE BOOL "Disable unused NRI NVTX support." FORCE)
set(NRI_ENABLE_NONE_SUPPORT OFF CACHE BOOL "Disable unused NRI NONE backend." FORCE)
set(NRI_ENABLE_VK_SUPPORT OFF CACHE BOOL "Disable unused NRI Vulkan backend." FORCE)
set(NRI_ENABLE_VALIDATION_SUPPORT OFF CACHE BOOL "Disable unused NRI validation backend." FORCE)
set(NRI_ENABLE_D3D11_SUPPORT OFF CACHE BOOL "Disable unused NRI D3D11 backend." FORCE)
set(NRI_ENABLE_D3D12_SUPPORT ON CACHE BOOL "Enable the NRI D3D12 backend." FORCE)
set(NRI_ENABLE_AMDAGS OFF CACHE BOOL "Disable unused NRI AMD AGS support." FORCE)
set(NRI_ENABLE_NVAPI OFF CACHE BOOL "Disable unused NRI NVAPI support." FORCE)
set(NRI_ENABLE_AGILITY_SDK_SUPPORT OFF CACHE BOOL "Use the renderer Agility SDK integration." FORCE)
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/NRI" "${CMAKE_BINARY_DIR}/NRI" EXCLUDE_FROM_ALL)
# Modify Begin:2026-08-17 by Hui
# NRI_API expands to a C++ export declaration that the Windows resource compiler cannot parse.
# The version resource is non-functional metadata, so exclude it from this isolated runtime build.
set_source_files_properties("${DX12_RENDERER_SOURCE_ROOT}/External/NRI/Resources/NRI.rc" TARGET_DIRECTORY NRI PROPERTIES HEADER_FILE_ONLY TRUE)
# Modify End

set(NRD_STATIC_LIBRARY OFF CACHE BOOL "Build NRD as a shared library." FORCE)
set(NRD_NRI OFF CACHE BOOL "Use the pinned top-level NRI target." FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "Disable unused NRD SPIR-V shader blobs." FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "Disable unused NRD DXBC shader blobs." FORCE)
set(NRD_EMBEDS_DXIL_SHADERS ON CACHE BOOL "Embed NRD DXIL shader blobs." FORCE)
set(NRD_SHADERS_PATH "${CMAKE_BINARY_DIR}/NRD/Shaders" CACHE PATH "NRD generated shader directory." FORCE)
set(SHADERMAKE_DXC_PATH "${DX12_RENDERER_DXC_EXECUTABLE}" CACHE FILEPATH "Renderer DXC used by ShaderMake." FORCE)
set(SHADERMAKE_FIND_DXC OFF CACHE BOOL "Do not download another DXC executable." FORCE)
file(MAKE_DIRECTORY "${NRD_SHADERS_PATH}")
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/NRD" "${CMAKE_BINARY_DIR}/NRD" EXCLUDE_FROM_ALL)
# Modify Begin:2026-08-17 by Hui
# NRD has the same C++ export macro in its non-functional Windows version resource.
set_source_files_properties("${DX12_RENDERER_SOURCE_ROOT}/External/NRD/Resources/NRD.rc" TARGET_DIRECTORY NRD PROPERTIES HEADER_FILE_ONLY TRUE)
# Modify End
# Modify Begin:2026-08-17 by Hui
# Root-level wrappers keep CMake --build target resolution independent of the generator.
add_custom_target(DX12RendererNRI)
add_dependencies(DX12RendererNRI NRI)
add_custom_target(DX12RendererNRD)
add_dependencies(DX12RendererNRD NRD)
# Modify End
# Modify End
