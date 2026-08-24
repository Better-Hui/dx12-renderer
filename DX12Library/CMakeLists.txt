cmake_minimum_required(VERSION 3.8.0)

project("DX12Library")

set(HEADER_FILES
        include/DX12Library/Application.h
        include/DX12Library/ApplicationResources.h
        include/DX12Library/Buffer.h
        include/DX12Library/ByteAddressBuffer.h
        include/DX12Library/Camera.h
        include/DX12Library/CommandList.h
        include/DX12Library/CommandListInternalAccess.h
        include/DX12Library/CommandQueue.h
        include/DX12Library/CommandQueueFailure.h
        include/DX12Library/ConstantBuffer.h
        include/DX12Library/Cubemap.h
        include/DX12Library/D3D12DeviceContext.h
        include/DX12Library/D3D12RenderContext.h
        include/DX12Library/D3D12RuntimeLifecycle.h
        include/DX12Library/Defines.h
        include/DX12Library/DiagnosticReporter.h
        include/DX12Library/DescriptorAllocation.h
        include/DX12Library/DescriptorAllocator.h
        include/DX12Library/DescriptorAllocatorPage.h
        include/DX12Library/DX12LibPCH.h
        include/DX12Library/DynamicDescriptorHeap.h
        include/DX12Library/Events.h
        include/DX12Library/FrameResourceRing.h
        include/DX12Library/Game.h
        include/DX12Library/GenerateMipsPso.h
        include/DX12Library/GpuTimestampProfiler.h
        include/DX12Library/GpuReadbackBuffer.h
        include/DX12Library/Helpers.h
        include/DX12Library/HighResolutionClock.h
        include/DX12Library/IndexBuffer.h
        include/DX12Library/MipGenerator.h
        include/DX12Library/PresentationController.h
        include/DX12Library/KeyCodes.h
        include/DX12Library/RenderTarget.h
        include/DX12Library/Resource.h
        include/DX12Library/ResourceUploader.h
        include/DX12Library/ResourceStateTransition.h
        include/DX12Library/ResourceStateRegistry.h
        include/DX12Library/ResourceStateTracker.h
        include/DX12Library/RootSignature.h
        include/DX12Library/ShaderUtils.h
        include/DX12Library/StructuredBuffer.h
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
        src/CommandListInternalAccess.cpp
        src/CommandQueue.cpp
        src/ConstantBuffer.cpp
        src/Cubemap.cpp
        src/D3D12RenderContext.cpp
        src/DiagnosticReporter.cpp
        src/DescriptorAllocation.cpp
        src/DescriptorAllocator.cpp
        src/DescriptorAllocatorPage.cpp
        src/DX12LibPCH.cpp
        src/DynamicDescriptorHeap.cpp
        src/Game.cpp
        src/FrameResourceRing.cpp
        src/GenerateMipsPso.cpp
        src/GpuTimestampProfiler.cpp
        src/GpuReadbackBuffer.cpp
        src/HighResolutionClock.cpp
        src/IndexBuffer.cpp
        src/MipGenerator.cpp
        src/RenderTarget.cpp
        src/Resource.cpp
        src/ResourceUploader.cpp
        src/ResourceStateTracker.cpp
        src/RootSignature.cpp
        src/ShaderUtils.cpp
        src/StructuredBuffer.cpp
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
target_compile_definitions(${TARGET_NAME} PUBLIC NOMINMAX)

if (DX12_RENDERER_ENABLE_D3D12_AGILITY)
        target_compile_definitions(${TARGET_NAME} PRIVATE
                DX12_RENDERER_ENABLE_D3D12_AGILITY=1
                DX12_RENDERER_AGILITY_SDK_VERSION=${DX12_RENDERER_AGILITY_SDK_VERSION})
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../CMakeIncludes/ProjectBasePost.cmake)

target_include_directories(${TARGET_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/DX12Library
        )

# Includes and libraries
target_include_directories(${TARGET_NAME}
        PUBLIC ${EXT_HEADER_FILES}  # headers of external libraries
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../WinPixEventRuntime/Include/WinPixEventRuntime/"
        )
target_link_libraries(${TARGET_NAME}
        PUBLIC DX12RendererAgilitySDK
        PUBLIC ${EXT_LIBRARIES}
        )
