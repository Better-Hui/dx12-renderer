cmake_minimum_required(VERSION 3.22.0)

# Use solution folders to organize projects
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Make sure DLL and EXE targets go to the same directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Output directory for static lib (.LIB)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for shared lib (.DLL)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for executables (.EXE)

cmake_policy(SET CMP0079 NEW)

project(LearningDirectX12 LANGUAGES CXX)

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

# Keep third-party targets out of the primary solution while retaining source builds.
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ThirdPartyDependencies.cmake)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ImGui.cmake)

# Libraries
add_subdirectory(DX12Library)
# Ray tracing framework code is built through Framework instead of a standalone DXR target.
add_subdirectory(Framework)
add_subdirectory(RenderGraph)

# Demo
add_subdirectory(Demos/RaytracingDemo)

# Set the startup project.
set_directory_properties(PROPERTIES
        VS_STARTUP_PROJECT RaytracingDemo
        )
