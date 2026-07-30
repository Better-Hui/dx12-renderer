cmake_minimum_required(VERSION 3.8.0)

project("RaytracingDemo")

set(HEADER_FILES
        include/Denoising/DenoiserController.h
        include/RaytracingDemo.h
        include/RenderGraph/RaytracingDemoRenderGraphBuilder.h
        include/PathTracing/PathTracingSceneBindings.h
        include/PathTracing/PathTracingPipelineController.h
        include/RenderGraph/RaytracingDemoGraphResources.h
        include/Passes/RaytracingDemoPasses.h
        include/Passes/CudaBloomPass.h
        include/Scene/SceneLightManager.h
        include/Scene/SceneLighting.h
        include/Scene/SceneResources.h
        )

set(SOURCE_FILES
        src/main.cpp
        src/Denoising/DenoiserController.cpp
        src/RaytracingDemoCamera.cpp
        src/RaytracingDemo.cpp
        src/RaytracingDemoUI.cpp
        src/RenderGraph/RaytracingDemoRenderGraphBuilder.cpp
        src/Passes/BaseResourcesPass.cpp
        src/Passes/DenoisePass.cpp
        src/Overlay/RaytracingDemoOverlay.cpp
        src/PathTracing/PathTracingSceneBindings.cpp
        src/PathTracing/PathTracingPipelineController.cpp
        src/Passes/PathTracingPass.cpp
        src/Passes/CudaBloomPass.cpp
        src/Passes/PostProcessPass.cpp
        src/RenderGraph/RaytracingDemoGraphResources.cpp
        src/Passes/SkyboxPass.cpp
        src/Scene/SceneLightManager.cpp
        src/Scene/SceneResources.cpp
        )

set(SHADER_FILES_RAYTRACING
        shaders/PathTracing/PathTracing.rt.hlsl
        )

set(SHADERS_HEADER_FILES
        shaders/Common/PathTracingConstants.hlsli
        shaders/Common/RayOffset.hlsli
        shaders/GBuffer/GBufferLayout.hlsli
        shaders/GBuffer/GBufferSampling.hlsli
        shaders/PathTracing/PathTracingTypes.hlsli
        shaders/PathTracing/PathTracingGeometry.hlsli
        shaders/PathTracing/PathTracingPayload.hlsli
        shaders/PathTracing/PathTracingRandom.hlsli
        shaders/PathTracing/PathTracing.rt.hlsli
        shaders/PathTracing/PathTracing.rayquery.hlsli
        shaders/PathTracing/PathTracingShared.hlsli
        shaders/PathTracing/PathTracingSurface.hlsli
        shaders/Scene/SceneCamera.hlsli
        # Modify Begin:2026-07-30 by BestHui
        shaders/Scene/SceneBindless.hlsli
        # Modify End
        shaders/Scene/SceneGeometry.hlsli
        shaders/Scene/SceneResources.hlsli
        shaders/Scene/SceneLighting.hlsli
        )
list(APPEND SHADERS_HEADER_FILES ${SHADER_FILES_RAYTRACING})

# Modify Begin:2026-07-30 by BestHui
set(SHADER_FILES_MESH
        shaders/GBuffer/GBuffer.ms.hlsl
        )
list(APPEND SHADERS_HEADER_FILES ${SHADER_FILES_MESH})
# Modify End

# Modify Begin:2026-07-28 by BestHui
set(SHADER_FILES_VERTEX
        shaders/GBuffer/GBuffer.vs.hlsl
        shaders/GBuffer/GBuffer.meshletindirect.vs.hlsl
        shaders/PostProcessing/DisplayComposite.vs.hlsl
        shaders/LightBillboard/LightBillboard.vs.hlsl
        shaders/Skybox/Skybox.vs.hlsl
        )

set(SHADER_FILES_PIXEL
        shaders/GBuffer/GBuffer.ps.hlsl
        shaders/GBuffer/GBuffer.meshletindirect.ps.hlsl
        shaders/PostProcessing/DisplayComposite.ps.hlsl
        shaders/LightBillboard/LightBillboard.ps.hlsl
        shaders/Skybox/Skybox.ps.hlsl
        )
# Modify End

set(SHADER_FILES_COMPUTE
        shaders/GBuffer/MeshletCull.cs.hlsl
        shaders/PathTracing/DirectLighting.cs.hlsl
        shaders/PathTracing/IndirectLighting.cs.hlsl
        shaders/PathTracing/LightingComposite.cs.hlsl
# Modify Begin:2026-07-28 by BestHui
        shaders/Skybox/Skybox.cs.hlsl
# Modify End
        )

# Modify Begin:2026-07-27 by BestHui
set(CUDA_SHADER_FILES
        shaders/CUDA/CudaBloom.cu
        )
list(APPEND SHADERS_HEADER_FILES ${CUDA_SHADER_FILES})
# Modify End

set(TARGET_NAME RaytracingDemo)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/DemoBase.cmake)

# Modify Begin:2026-07-27 by BestHui
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include" PREFIX "include" FILES ${HEADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${SOURCE_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/shaders" PREFIX "shaders" FILES
        ${SHADER_FILES_RAYTRACING}
        ${SHADERS_HEADER_FILES}
        ${SHADER_FILES_MESH}
        ${SHADER_FILES_VERTEX}
        ${SHADER_FILES_PIXEL}
        ${SHADER_FILES_COMPUTE}
        ${CUDA_SHADER_FILES}
        )
# Modify End

set_source_files_properties(${SHADER_FILES_RAYTRACING} PROPERTIES HEADER_FILE_ONLY TRUE)
# Modify Begin:2026-07-27 by BestHui
set_source_files_properties(${CUDA_SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
# Modify End
# Modify Begin:2026-07-30 by BestHui
set_source_files_properties(${SHADER_FILES_MESH} PROPERTIES HEADER_FILE_ONLY TRUE)
# Modify End

target_link_libraries(${TARGET_NAME}
        PUBLIC Framework
        PUBLIC RenderGraph
        )

# Modify Begin:2026-07-27 by BestHui
target_compile_options(${TARGET_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_link_options(${TARGET_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>")

find_program(CUDA_NVCC_EXECUTABLE
        NAMES nvcc
        HINTS "$ENV{CUDA_PATH}/bin" "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/bin"
        )

find_path(CUDA_INCLUDE_DIR
        NAMES cuda.h
        HINTS "$ENV{CUDA_PATH}/include" "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/include"
        )

find_library(CUDA_DRIVER_LIBRARY
        NAMES cuda
        HINTS "$ENV{CUDA_PATH}/lib/x64" "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/lib/x64"
        )

if (NOT CUDA_NVCC_EXECUTABLE OR NOT CUDA_INCLUDE_DIR OR NOT CUDA_DRIVER_LIBRARY)
        message(FATAL_ERROR "RaytracingDemo CUDA bloom requires CUDA Toolkit. Install with: winget install --id Nvidia.CUDA -e")
endif()

get_filename_component(CUDA_HOST_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
target_include_directories(${TARGET_NAME} PRIVATE "${CUDA_INCLUDE_DIR}")
target_link_libraries(${TARGET_NAME} PRIVATE "${CUDA_DRIVER_LIBRARY}")
set(CUDA_BLOOM_PTX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/CudaBloom.ptx")
# Modify End

set(NRD_ROOT "${CMAKE_SOURCE_DIR}/External/NRD")
set(NRI_ROOT "${CMAKE_SOURCE_DIR}/External/NRI")
set(NRD_BIN_DIR "${NRD_ROOT}/_Bin")
set(NRI_BIN_DIR "${NRD_ROOT}/_Bin")

target_include_directories(${TARGET_NAME} PRIVATE
        "${NRD_ROOT}/Include"
        "${NRD_ROOT}/Integration"
        "${NRI_ROOT}/Include"
        )

target_link_libraries(${TARGET_NAME}
        PRIVATE "${NRD_BIN_DIR}/NRD.lib"
        PRIVATE "${NRI_BIN_DIR}/NRI.lib"
        )

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NRD_BIN_DIR}/NRD.dll" "$<TARGET_FILE_DIR:${TARGET_NAME}>/NRD.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NRI_BIN_DIR}/NRI.dll" "$<TARGET_FILE_DIR:${TARGET_NAME}>/NRI.dll"
        VERBATIM
)

# Modify Begin:2026-07-21 by BestHui
set(PROJECT_DXC_EXECUTABLE "${CMAKE_SOURCE_DIR}/DXC/dxc.exe")
if (EXISTS "${PROJECT_DXC_EXECUTABLE}")
        set(DXC_EXECUTABLE "${PROJECT_DXC_EXECUTABLE}")
else()
        file(GLOB DXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
        list(SORT DXC_CANDIDATES)
        list(GET DXC_CANDIDATES -1 DXC_EXECUTABLE)
endif()
# Modify End

set(RAYTRACING_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILES_RAYTRACING}")
set(RAYTRACING_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/PathTracing.rt.cso")
# Modify Begin:2026-07-30 by BestHui
set(GBUFFER_MESH_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GBuffer/GBuffer.ms.hlsl")
set(GBUFFER_MESH_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/GBuffer.ms.cso")
# Modify End

# Modify Begin:2026-07-21 by BestHui
set(RAYTRACING_SHADER_MODEL lib_6_6)
if (DX12_RENDERER_ENABLE_SM610_LINALG)
        set(RAYTRACING_SHADER_MODEL lib_6_10)
        target_compile_definitions(${TARGET_NAME} PRIVATE DX12_RENDERER_ENABLE_SM610_LINALG=1)
endif()
# Modify End

add_custom_command(TARGET ${TARGET_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
# Modify Begin:2026-07-21 by BestHui
        COMMAND "${DXC_EXECUTABLE}" -T ${RAYTRACING_SHADER_MODEL} -HV 2021 -Fo "${RAYTRACING_SHADER_OUTPUT}" "${RAYTRACING_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/DXC/include" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/External/NRD/Shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
# Modify End
# Modify Begin:2026-07-30 by BestHui
        COMMAND "${DXC_EXECUTABLE}" -E main -T ms_6_6 -HV 2021 -Fo "${GBUFFER_MESH_SHADER_OUTPUT}" "${GBUFFER_MESH_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
# Modify End
# Modify Begin:2026-07-27 by BestHui
        COMMAND "${CUDA_NVCC_EXECUTABLE}" -ptx -O3 -arch=sm_75 -ccbin "${CUDA_HOST_COMPILER_DIR}" -Xcompiler=/utf-8 -o "${CUDA_BLOOM_PTX_OUTPUT}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CUDA/CudaBloom.cu"
# Modify End
        VERBATIM
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAYTRACING_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/PathTracing.rt.cso"
# Modify Begin:2026-07-30 by BestHui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GBUFFER_MESH_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/GBuffer.ms.cso"
# Modify End
# Modify Begin:2026-07-27 by BestHui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CUDA_BLOOM_PTX_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/CudaBloom.ptx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CUDA/CudaBloom.cu" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/CudaBloom.cu"
# Modify End
        VERBATIM
)
