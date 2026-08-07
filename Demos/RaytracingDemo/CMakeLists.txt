cmake_minimum_required(VERSION 3.8.0)

project("RaytracingDemo")

set(HEADER_FILES
        include/Denoising/DenoiserController.h
        # Modify Begin:2026-08-07 by BestHui
        include/Automation/RuntimeAutomationController.h
        include/Profiling/RenderGraphTimingHistory.h
        # Modify End
        include/RaytracingDemo.h
        include/RenderGraph/RaytracingDemoRenderGraphBuilder.h
        include/PathTracing/PathTracingSceneBindings.h
        include/PathTracing/PathTracingPipelineController.h
        include/RenderGraph/RaytracingDemoGraphResources.h
        include/Passes/RaytracingDemoPasses.h
        include/Passes/CudaBloomPass.h
        # Modify Begin:2026-07-30 by BestHui
        include/Passes/RaytracingDemoPassResources.h
        # Modify End
        include/Scene/SceneLightManager.h
        include/Scene/SceneLighting.h
        # Modify Begin:2026-07-30 by BestHui
        include/Scene/SceneResourceTypes.h
        include/Scene/SceneResourceBuilders.h
        include/Scene/SceneStressTestFactory.h
        # Modify End
        include/Scene/SceneResources.h
        # Modify Begin:2026-08-06 by BestHui
        include/UI/DemoLightEditor.h
        # Modify End
        )

set(SOURCE_FILES
        src/main.cpp
        src/Denoising/DenoiserController.cpp
        # Modify Begin:2026-08-07 by BestHui
        src/Automation/RuntimeAutomationController.cpp
        src/Profiling/RenderGraphTimingHistory.cpp
        # Modify End
        src/RaytracingDemoCamera.cpp
        src/RaytracingDemo.cpp
        src/RaytracingDemoUI.cpp
        # Modify Begin:2026-08-06 by BestHui
        src/UI/DemoLightEditor.cpp
        # Modify End
        src/RenderGraph/RaytracingDemoRenderGraphBuilder.cpp
        src/Passes/BaseResourcesPass.cpp
        src/Passes/DebugPass.cpp
        src/Passes/DenoisePass.cpp
        src/Overlay/RaytracingDemoOverlay.cpp
        src/PathTracing/PathTracingSceneBindings.cpp
        src/PathTracing/PathTracingPipelineController.cpp
        src/Passes/PathTracingPass.cpp
        src/Passes/ReSTIRDIPasses.cpp
        # Modify Begin:2026-08-07 by BestHui
        src/Passes/DLSSPass.cpp
        src/Passes/DLSSRayReconstructionPreparationPass.cpp
        # Modify End
        src/Passes/CudaBloomPass.cpp
        # Modify Begin:2026-07-30 by BestHui
        src/Passes/RaytracingDemoPassResources.cpp
        # Modify End
        src/Passes/PostProcessPass.cpp
        src/RenderGraph/RaytracingDemoGraphResources.cpp
        src/Passes/SkyboxPass.cpp
        src/Scene/SceneLightManager.cpp
        # Modify Begin:2026-07-30 by BestHui
        src/Scene/SceneResourceBuilders.cpp
        src/Scene/SceneStressTestFactory.cpp
        # Modify End
        src/Scene/SceneResources.cpp
        )

set(SHADER_FILES_RAYTRACING
        shaders/PathTracing/PathTracing.rt.hlsl
        # Modify Begin:2026-07-30 by BestHui
        shaders/PathTracing/PathTracing.softshadow.rt.hlsl
        # Modify End
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
        # Modify Begin:2026-08-06 by BestHui
        shaders/Scene/SceneGeometryBindless.hlsli
        # Modify End
        shaders/Scene/SceneGeometry.hlsli
        shaders/Scene/SceneResources.hlsli
        shaders/Scene/SceneLighting.hlsli
        )
list(APPEND SHADERS_HEADER_FILES ${SHADER_FILES_RAYTRACING})

# Modify Begin:2026-07-30 by BestHui
set(SHADER_FILES_MESH
# Modify Begin:2026-07-31 by BestHui
        shaders/GBuffer/GBuffer.task.as.hlsl
        shaders/GBuffer/GBuffer.task.ms.hlsl
# Modify End
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

set(RESTIR_DI_CORE_SHADER_FILES
        ../../Framework/shaders/ReSTIRDI/ReSTIRDI.RIS.cs.hlsl
        ../../Framework/shaders/ReSTIRDI/ReSTIRDI.Temporal.cs.hlsl
        ../../Framework/shaders/ReSTIRDI/ReSTIRDI.Spatial.cs.hlsl
        ../../Framework/shaders/ReSTIRDI/ReSTIRDI.Shade.cs.hlsl
        )

# Modify Begin:2026-08-06 by BestHui
set(RESTIR_DI_SHADER_FILES
        shaders/ReSTIRDI/ReSTIRDI.RIS.cs.hlsl
        shaders/ReSTIRDI/ReSTIRDI.Temporal.cs.hlsl
        shaders/ReSTIRDI/ReSTIRDI.Spatial.cs.hlsl
        shaders/ReSTIRDI/ReSTIRDI.Shade.cs.hlsl
        )
list(APPEND SHADERS_HEADER_FILES
        ${RESTIR_DI_CORE_SHADER_FILES}
        shaders/ReSTIRDI/ReSTIRDISceneAdapter.hlsli
        )
set_source_files_properties(${RESTIR_DI_CORE_SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
# Modify End

set(SHADER_FILES_COMPUTE
        shaders/GBuffer/MeshletCull.cs.hlsl
        shaders/PathTracing/DirectLighting.cs.hlsl
        # Modify Begin:2026-08-05 by BestHui
        ${RESTIR_DI_SHADER_FILES}
        # Modify End
        shaders/PathTracing/IndirectLighting.cs.hlsl
        # Modify Begin:2026-07-30 by BestHui
        shaders/PathTracing/DirectLighting.softshadow.cs.hlsl
        shaders/PathTracing/IndirectLighting.softshadow.cs.hlsl
        # Modify End
        shaders/PathTracing/LightingComposite.cs.hlsl
        # Modify Begin:2026-08-07 by BestHui
        shaders/Upscaling/DLSSRayReconstructionPrepare.cs.hlsl
        # Modify End
# Modify Begin:2026-07-28 by BestHui
        shaders/Skybox/Skybox.cs.hlsl
        shaders/Skybox/SkyboxEquirectangular.cs.hlsl
        shaders/Skybox/SkyboxCubemapStrip.cs.hlsl
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
set(DEMO_SHADER_FILES
        ${SHADER_FILES_RAYTRACING}
        ${SHADERS_HEADER_FILES}
        ${SHADER_FILES_MESH}
        ${SHADER_FILES_VERTEX}
        ${SHADER_FILES_PIXEL}
        ${SHADER_FILES_COMPUTE}
        ${CUDA_SHADER_FILES}
        )
list(REMOVE_ITEM DEMO_SHADER_FILES ${RESTIR_DI_CORE_SHADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/shaders" PREFIX "shaders" FILES ${DEMO_SHADER_FILES})
source_group(TREE "${CMAKE_SOURCE_DIR}/Framework/shaders/ReSTIRDI" PREFIX "Framework\\ReSTIRDI" FILES ${RESTIR_DI_CORE_SHADER_FILES})
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

# Modify Begin:2026-07-30 by BestHui
set(DX12_RENDERER_CUDA_TOOLKIT_ROOT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8" CACHE PATH "CUDA Toolkit 12.8 root used by DX12 renderer CUDA interop.")

find_program(DX12_RENDERER_CUDA_NVCC_EXECUTABLE
        NAMES nvcc
        HINTS "${DX12_RENDERER_CUDA_TOOLKIT_ROOT}/bin"
        NO_DEFAULT_PATH
        )

find_path(DX12_RENDERER_CUDA_INCLUDE_DIR
        NAMES cuda.h
        HINTS "${DX12_RENDERER_CUDA_TOOLKIT_ROOT}/include"
        NO_DEFAULT_PATH
        )

find_library(DX12_RENDERER_CUDA_DRIVER_LIBRARY
        NAMES cuda
        HINTS "${DX12_RENDERER_CUDA_TOOLKIT_ROOT}/lib/x64"
        NO_DEFAULT_PATH
        )

if (NOT DX12_RENDERER_CUDA_NVCC_EXECUTABLE OR NOT DX12_RENDERER_CUDA_INCLUDE_DIR OR NOT DX12_RENDERER_CUDA_DRIVER_LIBRARY)
        message(FATAL_ERROR "RaytracingDemo CUDA bloom requires CUDA Toolkit 12.8. Set DX12_RENDERER_CUDA_TOOLKIT_ROOT to the installed v12.8 directory.")
endif()
# Modify End

get_filename_component(CUDA_HOST_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
target_include_directories(${TARGET_NAME} PRIVATE "${DX12_RENDERER_CUDA_INCLUDE_DIR}")
target_link_libraries(${TARGET_NAME} PRIVATE "${DX12_RENDERER_CUDA_DRIVER_LIBRARY}")
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
        # Modify Begin:2026-08-07 by BestHui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DX12_RENDERER_DLSS_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/nvngx_dlss.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DX12_RENDERER_STREAMLINE_RUNTIME_FILES} "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Config/sl.interposer.json" "$<TARGET_FILE_DIR:${TARGET_NAME}>/sl.interposer.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DX12_RENDERER_STREAMLINE_ROOT}/sl.common.json" "$<TARGET_FILE_DIR:${TARGET_NAME}>/sl.common.json"
        # Modify End
        # Modify Begin:2026-08-03 by BestHui
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets/Scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Assets/Scenes/DefaultScene.json" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets/Scenes/DefaultScene.json"
        # Modify End
        VERBATIM
)

# Modify Begin:2026-07-21 by BestHui
set(DXC_EXECUTABLE "${DX12_RENDERER_DXC_EXECUTABLE}")
# Modify End

set(RAYTRACING_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/PathTracing/PathTracing.rt.hlsl")
set(RAYTRACING_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/PathTracing.rt.cso")
# Modify Begin:2026-07-30 by BestHui
set(RAYTRACING_SOFT_SHADOW_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/PathTracing/PathTracing.softshadow.rt.hlsl")
set(RAYTRACING_SOFT_SHADOW_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/PathTracing.softshadow.rt.cso")
# Modify End
# Modify Begin:2026-07-31 by BestHui
set(GBUFFER_TASK_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GBuffer/GBuffer.task.as.hlsl")
set(GBUFFER_TASK_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/GBuffer.task.as.cso")
set(GBUFFER_TASK_MESH_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GBuffer/GBuffer.task.ms.hlsl")
set(GBUFFER_TASK_MESH_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/GBuffer.task.ms.cso")
# Modify End

# Modify Begin:2026-07-30 by BestHui
set(RAYTRACING_SHADER_MODEL ${DX12_RENDERER_RAYTRACING_SHADER_MODEL})
# Modify End

add_custom_command(TARGET ${TARGET_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
# Modify Begin:2026-07-21 by BestHui
        COMMAND "${DXC_EXECUTABLE}" -T ${RAYTRACING_SHADER_MODEL} -HV 2021 -Fo "${RAYTRACING_SHADER_OUTPUT}" "${RAYTRACING_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/DXC/include" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_SOURCE_DIR}/External/NRD/Shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
# Modify End
# Modify Begin:2026-07-30 by BestHui
        COMMAND "${DXC_EXECUTABLE}" -T ${RAYTRACING_SHADER_MODEL} -HV 2021 -Fo "${RAYTRACING_SOFT_SHADOW_SHADER_OUTPUT}" "${RAYTRACING_SOFT_SHADOW_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/DXC/include" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_SOURCE_DIR}/External/NRD/Shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
# Modify End
# Modify Begin:2026-07-31 by BestHui
        COMMAND "${DXC_EXECUTABLE}" -E main -T as_${DX12_RENDERER_SHADER_MODEL_SUFFIX} -HV 2021 -Fo "${GBUFFER_TASK_SHADER_OUTPUT}" "${GBUFFER_TASK_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
        COMMAND "${DXC_EXECUTABLE}" -E main -T ms_${DX12_RENDERER_SHADER_MODEL_SUFFIX} -HV 2021 -Fo "${GBUFFER_TASK_MESH_SHADER_OUTPUT}" "${GBUFFER_TASK_MESH_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
# Modify End
# Modify Begin:2026-07-27 by BestHui
        COMMAND "${DX12_RENDERER_CUDA_NVCC_EXECUTABLE}" -ptx -O3 -arch=sm_75 -ccbin "${CUDA_HOST_COMPILER_DIR}" -Xcompiler=/utf-8 -o "${CUDA_BLOOM_PTX_OUTPUT}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CUDA/CudaBloom.cu"
# Modify End
        VERBATIM
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAYTRACING_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/PathTracing.rt.cso"
# Modify Begin:2026-07-30 by BestHui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAYTRACING_SOFT_SHADOW_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/PathTracing.softshadow.rt.cso"
# Modify End
# Modify Begin:2026-07-31 by BestHui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GBUFFER_TASK_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/GBuffer.task.as.cso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GBUFFER_TASK_MESH_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/GBuffer.task.ms.cso"
# Modify End
# Modify Begin:2026-07-27 by BestHui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CUDA_BLOOM_PTX_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/CudaBloom.ptx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CUDA/CudaBloom.cu" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/CudaBloom.cu"
# Modify End
        VERBATIM
)
