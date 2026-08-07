cmake_minimum_required(VERSION 3.8.0)

# Use solution folders to organize projects
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Make sure DLL and EXE targets go to the same directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Output directory for static lib (.LIB)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for shared lib (.DLL)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for executables (.EXE)

cmake_policy(SET CMP0079 NEW)

project(LearningDirectX12 LANGUAGES CXX)

# Modify Begin:2026-07-30 by BestHui
set(DX12_RENDERER_SHADER_MODEL_VERSION "6.9")
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
