cmake_minimum_required(VERSION 3.30)

# Keep standalone third-party targets on the C++17 baseline required by NRI.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# The renderer consumes these libraries through imported targets from the
# parent project. Keep all vendored implementation projects in this isolated
# build tree so they cannot become projects in the renderer solution.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared third-party libraries." FORCE)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/meshoptimizer"
        "${CMAKE_BINARY_DIR}/meshoptimizer" EXCLUDE_FROM_ALL)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/assimp"
        "${CMAKE_BINARY_DIR}/assimp" EXCLUDE_FROM_ALL)

set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DX11 OFF CACHE BOOL "" FORCE)
set(BUILD_DX12 ON CACHE BOOL "" FORCE)
set(ENABLE_OPENEXR_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/DirectXTex"
        "${CMAKE_BINARY_DIR}/DirectXTex" EXCLUDE_FROM_ALL)
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/DirectXMesh"
        "${CMAKE_BINARY_DIR}/DirectXMesh" EXCLUDE_FROM_ALL)

set(IMATH_INSTALL ON CACHE BOOL "" FORCE)
set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(PYTHON OFF CACHE BOOL "" FORCE)
set(PYBIND11 OFF CACHE BOOL "" FORCE)
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/Imath"
        "${CMAKE_BINARY_DIR}/Imath" EXCLUDE_FROM_ALL)

# OpenEXR can consume the Imath target directly in this unified isolated build.
if (TARGET Imath AND NOT TARGET Imath::Imath)
        add_library(Imath::Imath ALIAS Imath)
endif()
if (TARGET ImathConfig AND NOT TARGET Imath::Config)
        add_library(Imath::Config ALIAS ImathConfig)
endif()

# OpenEXR uses the local Imath target and its vendored compression sources.
# This avoids a network package lookup while preserving the pinned submodules.
set(OPENEXR_FORCE_INTERNAL_IMATH ON CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_DEFLATE ON CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_ZSTD ON CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_OPENJPH ON CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${DX12_RENDERER_SOURCE_ROOT}/External/OpenEXR"
        "${CMAKE_BINARY_DIR}/OpenEXR" EXCLUDE_FROM_ALL)

# OpenEXR main currently uses the older Imath namespace macro names while the
# pinned Imath checkout exposes the shorter aliases.
foreach(DX12_RENDERER_OPENEXR_TARGET OpenEXR OpenEXRCore Iex IlmThread)
        if (TARGET ${DX12_RENDERER_OPENEXR_TARGET})
                target_compile_definitions(${DX12_RENDERER_OPENEXR_TARGET}
                        PRIVATE
                        IMATH_INTERNAL_NAMESPACE_HEADER_ENTER=IMATH_INTERNAL_NS_ENTER
                        IMATH_INTERNAL_NAMESPACE_HEADER_EXIT=IMATH_INTERNAL_NS_EXIT)
        endif()
endforeach()

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
# NRI_API expands to a C++ export declaration that the Windows resource compiler cannot parse.
# The version resource is non-functional metadata, so exclude it from this isolated runtime build.
set_source_files_properties("${DX12_RENDERER_SOURCE_ROOT}/External/NRI/Resources/NRI.rc" TARGET_DIRECTORY NRI PROPERTIES HEADER_FILE_ONLY TRUE)

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
# NRD has the same C++ export macro in its non-functional Windows version resource.
set_source_files_properties("${DX12_RENDERER_SOURCE_ROOT}/External/NRD/Resources/NRD.rc" TARGET_DIRECTORY NRD PROPERTIES HEADER_FILE_ONLY TRUE)
# Root-level wrappers keep CMake --build target resolution independent of the generator.
add_custom_target(DX12RendererNRI)
add_dependencies(DX12RendererNRI NRI)
add_custom_target(DX12RendererNRD)
add_dependencies(DX12RendererNRD NRD)

add_custom_target(DX12RendererExternal)
add_dependencies(DX12RendererExternal
        meshoptimizer
        assimp
        DirectXTex
        DirectXMesh
        Imath
        OpenEXR
        OpenEXRCore
        Iex
        IlmThread)
