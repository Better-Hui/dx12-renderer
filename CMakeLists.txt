cmake_minimum_required(VERSION 3.22.0)

# Use solution folders to organize projects
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Make sure DLL and EXE targets go to the same directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Output directory for static lib (.LIB)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for shared lib (.DLL)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for executables (.EXE)

cmake_policy(SET CMP0079 NEW)

project(LearningDirectX12 LANGUAGES CXX)

# Modify Begin:2026-07-30 by BestHui
set(DX12_RENDERER_SHADER_MODEL_VERSION "6.8")
string(REPLACE "." "_" DX12_RENDERER_SHADER_MODEL_SUFFIX "${DX12_RENDERER_SHADER_MODEL_VERSION}")
set(DX12_RENDERER_RAYTRACING_SHADER_MODEL "lib_${DX12_RENDERER_SHADER_MODEL_SUFFIX}")
set(DX12_RENDERER_ENABLE_D3D12_AGILITY ON)
set(DX12_RENDERER_AGILITY_SDK_VERSION "619")
set(DX12_RENDERER_AGILITY_RUNTIME_DIRECTORY "${CMAKE_SOURCE_DIR}/D3D12")
set(DX12_RENDERER_AGILITY_INCLUDE_DIRECTORY "${CMAKE_SOURCE_DIR}/External/AgilitySDK/include")
if (NOT EXISTS "${DX12_RENDERER_AGILITY_INCLUDE_DIRECTORY}/d3d12.h")
        message(FATAL_ERROR "DX12 renderer requires the Agility SDK ${DX12_RENDERER_AGILITY_SDK_VERSION} C++ headers.")
endif()
foreach(DX12_RENDERER_AGILITY_RUNTIME_FILE
        D3D12Core.dll
        d3d12SDKLayers.dll)
        if (NOT EXISTS "${DX12_RENDERER_AGILITY_RUNTIME_DIRECTORY}/${DX12_RENDERER_AGILITY_RUNTIME_FILE}")
                message(FATAL_ERROR "DX12 renderer requires ${DX12_RENDERER_AGILITY_RUNTIME_FILE} from the Agility SDK ${DX12_RENDERER_AGILITY_SDK_VERSION} redistributable.")
        endif()
endforeach()
set(DX12_RENDERER_DXC_EXECUTABLE "${CMAKE_SOURCE_DIR}/DXC/dxc.exe")
if (NOT EXISTS "${DX12_RENDERER_DXC_EXECUTABLE}")
        file(GLOB DX12_RENDERER_DXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
        list(SORT DX12_RENDERER_DXC_CANDIDATES)
        list(GET DX12_RENDERER_DXC_CANDIDATES -1 DX12_RENDERER_DXC_EXECUTABLE)
endif()
if (NOT EXISTS "${DX12_RENDERER_DXC_EXECUTABLE}")
        message(FATAL_ERROR "DX12 renderer requires dxc.exe for Shader Model ${DX12_RENDERER_SHADER_MODEL_VERSION} shader compilation.")
endif()

message(STATUS "DX12 renderer shader model baseline: ${DX12_RENDERER_SHADER_MODEL_VERSION}")
message(STATUS "DX12 renderer Agility SDK version: ${DX12_RENDERER_AGILITY_SDK_VERSION}")
message(STATUS "DX12 renderer DXC executable: ${DX12_RENDERER_DXC_EXECUTABLE}")
# Modify End

# Modify Begin:2026-08-07 by BestHui
set(DX12_RENDERER_DLSS_ROOT "${CMAKE_SOURCE_DIR}/External/DLSS" CACHE PATH "DLSS SDK root used by DX12 renderer.")
set(DX12_RENDERER_DLSS_INCLUDE_DIRECTORY "${DX12_RENDERER_DLSS_ROOT}/include")
set(DX12_RENDERER_DLSS_LIBRARY "${DX12_RENDERER_DLSS_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
set(DX12_RENDERER_DLSS_RUNTIME_DLL "${DX12_RENDERER_DLSS_ROOT}/lib/Windows_x86_64/rel/nvngx_dlss.dll")
foreach(DX12_RENDERER_DLSS_FILE
        "${DX12_RENDERER_DLSS_INCLUDE_DIRECTORY}/nvsdk_ngx.h"
        "${DX12_RENDERER_DLSS_LIBRARY}"
        "${DX12_RENDERER_DLSS_RUNTIME_DLL}")
        if (NOT EXISTS "${DX12_RENDERER_DLSS_FILE}")
                message(FATAL_ERROR "DX12 renderer requires the DLSS SDK file: ${DX12_RENDERER_DLSS_FILE}")
        endif()
endforeach()
# Modify End

# Modify Begin:2026-08-07 by BestHui
set(DX12_RENDERER_STREAMLINE_ROOT "${CMAKE_SOURCE_DIR}/External/Streamline" CACHE PATH "Streamline SDK root used by DLSS Frame Generation and Ray Reconstruction.")
set(DX12_RENDERER_STREAMLINE_INCLUDE_DIRECTORY "${DX12_RENDERER_STREAMLINE_ROOT}/include")
set(DX12_RENDERER_STREAMLINE_LIBRARY "${DX12_RENDERER_STREAMLINE_ROOT}/lib/x64/sl.interposer.lib")
set(DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY "${DX12_RENDERER_STREAMLINE_ROOT}/bin/x64/development")
set(DX12_RENDERER_STREAMLINE_RUNTIME_FILES
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/sl.interposer.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/sl.common.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/sl.dlss.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/sl.pcl.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/sl.reflex.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/sl.dlss_g.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/sl.dlss_d.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/nvngx_dlssg.dll"
        "${DX12_RENDERER_STREAMLINE_RUNTIME_DIRECTORY}/nvngx_dlssd.dll")
foreach(DX12_RENDERER_STREAMLINE_FILE
        "${DX12_RENDERER_STREAMLINE_INCLUDE_DIRECTORY}/sl.h"
        "${DX12_RENDERER_STREAMLINE_LIBRARY}"
        ${DX12_RENDERER_STREAMLINE_RUNTIME_FILES}
        "${DX12_RENDERER_STREAMLINE_ROOT}/sl.interposer.json"
        "${DX12_RENDERER_STREAMLINE_ROOT}/sl.common.json")
        if (NOT EXISTS "${DX12_RENDERER_STREAMLINE_FILE}")
                message(FATAL_ERROR "DX12 renderer requires the Streamline SDK file: ${DX12_RENDERER_STREAMLINE_FILE}")
        endif()
endforeach()
# Modify End

# Modify Begin:2026-08-10 by BestHui
# Build NRD/NRI from their pinned source submodules. Only the D3D12 backend is required by Framework.
set(NRI_STATIC_LIBRARY OFF CACHE BOOL "Build NRI as a shared library." FORCE)
set(NRI_ENABLE_NVTX_SUPPORT OFF CACHE BOOL "Disable unused NRI NVTX support." FORCE)
set(NRI_ENABLE_NONE_SUPPORT OFF CACHE BOOL "Disable unused NRI NONE backend." FORCE)
set(NRI_ENABLE_VK_SUPPORT OFF CACHE BOOL "Disable unused NRI Vulkan backend." FORCE)
set(NRI_ENABLE_VALIDATION_SUPPORT OFF CACHE BOOL "Disable unused NRI validation backend." FORCE)
set(NRI_ENABLE_D3D11_SUPPORT OFF CACHE BOOL "Disable unused NRI D3D11 backend." FORCE)
set(NRI_ENABLE_D3D12_SUPPORT ON CACHE BOOL "Enable the NRI D3D12 backend." FORCE)
set(NRI_ENABLE_AMDAGS OFF CACHE BOOL "Disable unused NRI AMD AGS support." FORCE)
set(NRI_ENABLE_NVAPI OFF CACHE BOOL "Disable unused NRI NVAPI support." FORCE)
set(NRI_ENABLE_AGILITY_SDK_SUPPORT OFF CACHE BOOL "Use this project's Agility SDK integration." FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/External/NRI" "${CMAKE_BINARY_DIR}/External/NRI" EXCLUDE_FROM_ALL)

set(NRD_STATIC_LIBRARY OFF CACHE BOOL "Build NRD as a shared library." FORCE)
set(NRD_NRI OFF CACHE BOOL "Use the pinned top-level NRI target." FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "Disable unused NRD SPIR-V shader blobs." FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "Disable unused NRD DXBC shader blobs." FORCE)
set(NRD_EMBEDS_DXIL_SHADERS ON CACHE BOOL "Embed NRD DXIL shader blobs." FORCE)
set(DX12_RENDERER_NRD_SHADER_INCLUDE_DIRECTORY "${CMAKE_BINARY_DIR}/External/NRD/Shaders")
set(NRD_SHADERS_PATH "${DX12_RENDERER_NRD_SHADER_INCLUDE_DIRECTORY}" CACHE PATH "NRD generated shader directory." FORCE)
file(MAKE_DIRECTORY "${DX12_RENDERER_NRD_SHADER_INCLUDE_DIRECTORY}")
add_subdirectory("${CMAKE_SOURCE_DIR}/External/NRD" "${CMAKE_BINARY_DIR}/External/NRD" EXCLUDE_FROM_ALL)
# Modify End

# Libraries
add_subdirectory(DX12Library)
# Modify Begin:2026-07-21 by BestHui
# Ray tracing framework code is built through Framework instead of a standalone DXR target.
# Modify End
add_subdirectory(Framework)
add_subdirectory(RenderGraph)

# Demos
# Modify Begin:2026-07-29 by BestHui
foreach(LOCAL_DEMO
        AnimationsDemo
        DeferredLightingDemo
        LightingDemo
        ToonDemo
        GrassDemo
        RenderGraphDemo
        MeshletsDemo)
        if (EXISTS "${CMAKE_SOURCE_DIR}/Demos/${LOCAL_DEMO}/CMakeLists.txt")
                add_subdirectory("Demos/${LOCAL_DEMO}")
        endif()
endforeach()
add_subdirectory(Demos/RaytracingDemo)
# Modify End

# Set the startup project.
set_directory_properties(PROPERTIES
        # Modify Begin:2026-07-29 by BestHui
        VS_STARTUP_PROJECT RaytracingDemo
        # Modify End
        )
