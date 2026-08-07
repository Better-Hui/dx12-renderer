cmake_minimum_required(VERSION 3.8.0)

project("DX12Library")

set(HEADER_FILES
        include/d3dx12.h
        include/DX12Library/Application.h
        include/DX12Library/ApplicationResources.h
        include/DX12Library/Buffer.h
        include/DX12Library/ByteAddressBuffer.h
        include/DX12Library/Camera.h
        include/DX12Library/CommandList.h
        include/DX12Library/CommandQueue.h
        include/DX12Library/ConstantBuffer.h
        include/DX12Library/Cubemap.h
        # Modify Begin:2026-07-28 by BestHui
        include/DX12Library/D3D12DeviceContext.h
        include/DX12Library/D3D12RenderContext.h
        # Modify End
        include/DX12Library/Defines.h
        include/DX12Library/DescriptorAllocation.h
        include/DX12Library/DescriptorAllocator.h
        include/DX12Library/DescriptorAllocatorPage.h
        include/DX12Library/DX12LibPCH.h
        include/DX12Library/DynamicDescriptorHeap.h
        include/DX12Library/Events.h
        # Modify Begin:2026-07-28 by BestHui
        include/DX12Library/FrameResourceRing.h
        # Modify End
        include/DX12Library/Game.h
        include/DX12Library/GenerateMipsPso.h
        # Modify Begin:2026-07-29 by BestHui
        include/DX12Library/GpuTimestampProfiler.h
        # Modify End
        include/DX12Library/Helpers.h
        include/DX12Library/HighResolutionClock.h
        include/DX12Library/IndexBuffer.h
        include/DX12Library/KeyCodes.h
        include/DX12Library/RenderTarget.h
        include/DX12Library/Resource.h
        # Modify Begin:2026-07-30 by BestHui
        include/DX12Library/ResourceStateTransition.h
        # Modify End
        # Modify Begin:2026-07-30 by BestHui
        include/DX12Library/ResourceStateRegistry.h
        # Modify End
        include/DX12Library/ResourceStateTracker.h
        include/DX12Library/RootSignature.h
        include/DX12Library/ShaderUtils.h
        include/DX12Library/StructuredBuffer.h
        # Modify Begin:2026-08-07 by BestHui
        include/DX12Library/StreamlineRuntime.h
        # Modify End
        include/DX12Library/Texture.h
        include/DX12Library/TextureUsageType.h
        include/DX12Library/ThreadSafeQueue.h
        include/DX12Library/UploadBuffer.h
        include/DX12Library/VertexBuffer.h
        include/DX12Library/Window.h
        include/DX12Library/ClearValue.h 
        include/DX12Library/RenderTargetFormats.h
        include/DX12Library/RenderTargetState.h
        include/DX12Library/DXGIExtensions.h
        )

set(SOURCE_FILES
        
        src/Application.cpp
        src/Buffer.cpp
        src/ByteAddressBuffer.cpp
        src/Camera.cpp
        src/CommandList.cpp
        src/CommandQueue.cpp
        src/ConstantBuffer.cpp
        src/Cubemap.cpp
        # Modify Begin:2026-07-28 by BestHui
        src/D3D12RenderContext.cpp
        # Modify End
        src/DescriptorAllocation.cpp
        src/DescriptorAllocator.cpp
        src/DescriptorAllocatorPage.cpp
        src/DX12LibPCH.cpp
        src/DynamicDescriptorHeap.cpp
        src/Game.cpp
        # Modify Begin:2026-07-28 by BestHui
        src/FrameResourceRing.cpp
        # Modify End
        src/GenerateMipsPso.cpp
        # Modify Begin:2026-07-29 by BestHui
        src/GpuTimestampProfiler.cpp
        # Modify End
        src/HighResolutionClock.cpp
        src/IndexBuffer.cpp
        src/RenderTarget.cpp
        src/Resource.cpp
        src/ResourceStateTracker.cpp
        src/RootSignature.cpp
        src/ShaderUtils.cpp
        src/StructuredBuffer.cpp
        # Modify Begin:2026-08-07 by BestHui
        src/StreamlineRuntime.cpp
        # Modify End
        src/Texture.cpp
        src/UploadBuffer.cpp
        src/VertexBuffer.cpp
        src/Window.cpp
        src/ClearValue.cpp
        src/DXGIExtensions.cpp
        )

set(SHADER_FILES_VERTEX

        )

set(SHADER_FILES_PIXEL

        )

set(SHADER_FILES_COMPUTE
        shaders/GenerateMips_CS.hlsl
        )

set(TARGET_NAME DX12Library)
set(SHADERS_OUTPUT_HEADERS TRUE)
include(${CMAKE_CURRENT_SOURCE_DIR}/../CMakeIncludes/ProjectBase.cmake)

# External headers and libraries
set(EXT_HEADER_FILES
        d3dcompiler.h
        )
set(EXT_LIBRARIES
        d3d12.lib
        dxcompiler.lib
        d3dcompiler.lib
        shlwapi.lib
        dxgi.lib
        dxguid.lib
        "${CMAKE_CURRENT_SOURCE_DIR}/../WinPixEventRuntime/bin/WinPixEventRuntime.lib"
        )

# Add source to this project's executable.
add_library(${TARGET_NAME} STATIC ${HEADER_FILES} ${SOURCE_FILES} ${SHADER_FILES} ${SHADERS_HEADER_FILES})

# Modify Begin:2026-07-30 by BestHui
target_include_directories(${TARGET_NAME} BEFORE PUBLIC "${DX12_RENDERER_AGILITY_INCLUDE_DIRECTORY}")

if (DX12_RENDERER_ENABLE_D3D12_AGILITY)
        target_compile_definitions(${TARGET_NAME} PUBLIC
                DX12_RENDERER_ENABLE_D3D12_AGILITY=1
                DX12_RENDERER_AGILITY_SDK_VERSION=${DX12_RENDERER_AGILITY_SDK_VERSION})
endif()
# Modify End

include(${CMAKE_CURRENT_SOURCE_DIR}/../CMakeIncludes/ProjectBasePost.cmake)

target_include_directories(${TARGET_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/DX12Library
        # Modify Begin:2026-08-07 by BestHui
        PRIVATE "${DX12_RENDERER_STREAMLINE_INCLUDE_DIRECTORY}"
        # Modify End
        )

# Includes and libraries
target_include_directories(${TARGET_NAME}
        PUBLIC ${EXT_HEADER_FILES}  # headers of external libraries
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../WinPixEventRuntime/Include/WinPixEventRuntime/"
        )
target_link_libraries(${TARGET_NAME}
        PUBLIC ${EXT_LIBRARIES}
        # Modify Begin:2026-08-07 by BestHui
        PUBLIC "${DX12_RENDERER_STREAMLINE_LIBRARY}"
        # Modify End
        )


# Packages

# assimp
find_package(assimp CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC assimp::assimp)

# DirectXTex
find_package(directxtex CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC Microsoft::DirectXTex)

# OpenEXR
find_package(OpenEXR CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC OpenEXR::OpenEXR)

# DirectXMesh
find_package(directxmesh CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC Microsoft::DirectXMesh)
