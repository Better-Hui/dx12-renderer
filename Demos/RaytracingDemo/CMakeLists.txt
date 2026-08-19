cmake_minimum_required(VERSION 3.8.0)

project("RaytracingDemo")

set(HEADER_FILES
        include/Denoising/DenoiserController.h
        include/Automation/RuntimeAutomationController.h
        include/Automation/RaytracingDemoAutomation.h
        include/Profiling/ProfilerDisplayController.h
        include/Profiling/RenderGraphTimingHistory.h
        include/RaytracingDemo.h
        include/RenderGraph/RaytracingDemoRenderGraphBuilder.h
        include/PathTracing/PathTracingSceneBindings.h
        include/PathTracing/PathTracingPipelineController.h
        include/RenderGraph/RaytracingDemoGraphResources.h
        include/RenderGraph/RaytracingDemoRenderPipelineController.h
        include/Passes/RaytracingDemoPasses.h
        include/Passes/CudaBloomPass.h
        include/Passes/RaytracingDemoPassResources.h
        include/Scene/SceneLightManager.h
        include/Scene/RaytracingDemoSceneRuntimeController.h
        include/Scene/SceneLighting.h
        include/Scene/SceneResourceTypes.h
        include/Scene/SceneResourceBuilders.h
        include/Scene/SceneStressTestFactory.h
        include/Scene/SceneResources.h
        include/UI/DemoLightEditor.h
        )

set(SOURCE_FILES
        src/main.cpp
        src/Denoising/DenoiserController.cpp
        src/Automation/RuntimeAutomationController.cpp
        src/Automation/RaytracingDemoAutomation.cpp
        src/Profiling/ProfilerDisplayController.cpp
        src/Profiling/RenderGraphTimingHistory.cpp
        src/RaytracingDemoCamera.cpp
        src/RaytracingDemo.cpp
        src/RaytracingDemoUI.cpp
        src/UI/DemoLightEditor.cpp
        src/RenderGraph/RaytracingDemoRenderGraphBuilder.cpp
        src/RenderGraph/RaytracingDemoRenderPipelineController.cpp
        src/Passes/BaseResourcesPass.cpp
        src/Passes/DebugPass.cpp
        src/Passes/DenoisePass.cpp
        src/Overlay/RaytracingDemoOverlay.cpp
        src/PathTracing/PathTracingSceneBindings.cpp
        src/PathTracing/PathTracingPipelineController.cpp
        src/Passes/PathTracingPass.cpp
        src/Passes/ReSTIRDIPasses.cpp
        src/Passes/ReSTIRGIPasses.cpp
        src/Passes/DLSSPass.cpp
        src/Passes/DLSSRayReconstructionPreparationPass.cpp
        src/Passes/CudaBloomPass.cpp
        src/Passes/RaytracingDemoPassResources.cpp
        src/Passes/PostProcessPass.cpp
        src/RenderGraph/RaytracingDemoGraphResources.cpp
        src/Passes/SkyboxPass.cpp
        src/Scene/SceneLightManager.cpp
        src/Scene/RaytracingDemoSceneRuntimeController.cpp
        src/Scene/SceneResourceBuilders.cpp
        src/Scene/SceneStressTestFactory.cpp
        src/Scene/SceneResources.cpp
        )

set(SHADER_FILES_RAYTRACING
        shaders/PathTracing/PathTracing.rt.hlsl
        shaders/PathTracing/PathTracing.softshadow.rt.hlsl
        )

set(SHADERS_HEADER_FILES
        shaders/CUDA/CudaBloom.cu
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
        shaders/Scene/SceneGeometryBindless.hlsli
        shaders/Scene/SceneGeometry.hlsli
        shaders/Scene/SceneResources.hlsli
        shaders/Scene/SceneLighting.hlsli
        )
list(APPEND SHADERS_HEADER_FILES ${SHADER_FILES_RAYTRACING})

set(SHADER_FILES_MESH
        shaders/GBuffer/GBuffer.task.as.hlsl
        shaders/GBuffer/GBuffer.task.ms.hlsl
        )
list(APPEND SHADERS_HEADER_FILES ${SHADER_FILES_MESH})

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

# These files only bind RaytracingDemo scene data to Framework ReSTIR code.
# Keep that boundary explicit in the IDE instead of displaying them as ReSTIR
# implementations owned by the demo.
set(RESTIR_DI_SHADER_FILES
        shaders/ReSTIRDI/ReSTIRDI.RIS.cs.hlsl
        shaders/ReSTIRDI/ReSTIRDI.Temporal.cs.hlsl
        shaders/ReSTIRDI/ReSTIRDI.Spatial.cs.hlsl
        shaders/ReSTIRDI/ReSTIRDI.Shade.cs.hlsl
        )
list(APPEND SHADERS_HEADER_FILES
        shaders/ReSTIRDI/ReSTIRDISceneAdapter.hlsli
        )

set(RESTIR_GI_SHADER_FILES
        shaders/ReSTIRGI/ReSTIRGI.Initial.cs.hlsl
        shaders/ReSTIRGI/ReSTIRGI.Temporal.cs.hlsl
        shaders/ReSTIRGI/ReSTIRGI.Spatial.cs.hlsl
        shaders/ReSTIRGI/ReSTIRGI.Shade.cs.hlsl
        )
list(APPEND SHADERS_HEADER_FILES
        shaders/ReSTIRGI/ReSTIRGISceneAdapter.hlsli
        )

set(SHADER_FILES_COMPUTE
        shaders/GBuffer/MeshletCull.cs.hlsl
        shaders/PathTracing/DirectLighting.cs.hlsl
        ${RESTIR_DI_SHADER_FILES}
        ${RESTIR_GI_SHADER_FILES}
        shaders/PathTracing/IndirectLighting.cs.hlsl
        shaders/PathTracing/DirectLighting.softshadow.cs.hlsl
        shaders/PathTracing/IndirectLighting.softshadow.cs.hlsl
        shaders/PathTracing/LightingComposite.cs.hlsl
        shaders/Upscaling/DLSSRayReconstructionPrepare.cs.hlsl
        shaders/Skybox/Skybox.cs.hlsl
        shaders/Skybox/SkyboxEquirectangular.cs.hlsl
        shaders/Skybox/SkyboxCubemapStrip.cs.hlsl
        )

set(TARGET_NAME RaytracingDemo)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/DemoBase.cmake)

set_source_files_properties(${SHADER_FILES_RAYTRACING} PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties(${SHADER_FILES_MESH} PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties(shaders/CUDA/CudaBloom.cu PROPERTIES HEADER_FILE_ONLY TRUE)

target_link_libraries(${TARGET_NAME}
        PUBLIC Framework
        PRIVATE FrameworkNvidiaFeatures
        PUBLIC RenderGraph
        )

find_program(DX12_RENDERER_CUDA_NVCC_EXECUTABLE
        NAMES nvcc
        HINTS "${DX12_RENDERER_CUDA_TOOLKIT_ROOT}/bin"
        NO_DEFAULT_PATH
        )
if (NOT DX12_RENDERER_CUDA_NVCC_EXECUTABLE)
        message(FATAL_ERROR "RaytracingDemo CUDA Bloom requires CUDA Toolkit 12.8 nvcc. Set DX12_RENDERER_CUDA_TOOLKIT_ROOT to the installed v12.8 directory.")
endif()

set(DX12_RENDERER_CUDA_BLOOM_PTX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/CudaBloom.ptx")
get_filename_component(DX12_RENDERER_CUDA_HOST_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_custom_command(TARGET ${TARGET_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
        COMMAND "${DX12_RENDERER_CUDA_NVCC_EXECUTABLE}" -ptx -O3 -arch=sm_75 -ccbin "${DX12_RENDERER_CUDA_HOST_COMPILER_DIR}" -Xcompiler=/utf-8 -o "${DX12_RENDERER_CUDA_BLOOM_PTX_OUTPUT}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CUDA/CudaBloom.cu"
        VERBATIM
)

# Build NRI and NRD in their own generated solution before shaders or linking need their artifacts.
dx12_renderer_add_third_party_prebuild(${TARGET_NAME})

target_compile_options(${TARGET_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DX12_RENDERER_DLSS_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/nvngx_dlss.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DX12_RENDERER_STREAMLINE_RUNTIME_FILES} "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Config/sl.interposer.json" "$<TARGET_FILE_DIR:${TARGET_NAME}>/sl.interposer.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DX12_RENDERER_STREAMLINE_ROOT}/sl.common.json" "$<TARGET_FILE_DIR:${TARGET_NAME}>/sl.common.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:NRI>" "$<TARGET_FILE_DIR:${TARGET_NAME}>/NRI.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:NRD>" "$<TARGET_FILE_DIR:${TARGET_NAME}>/NRD.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Config/RaytracingDemo.ini" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Config/RaytracingDemo.ini"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets/Scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Assets/Scenes/DefaultScene.json" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets/Scenes/DefaultScene.json"
        VERBATIM
)

set(DXC_EXECUTABLE "${DX12_RENDERER_DXC_EXECUTABLE}")

set(RAYTRACING_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/PathTracing/PathTracing.rt.hlsl")
set(RAYTRACING_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/PathTracing.rt.cso")
set(RAYTRACING_SOFT_SHADOW_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/PathTracing/PathTracing.softshadow.rt.hlsl")
set(RAYTRACING_SOFT_SHADOW_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/PathTracing.softshadow.rt.cso")
set(GBUFFER_TASK_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GBuffer/GBuffer.task.as.hlsl")
set(GBUFFER_TASK_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/GBuffer.task.as.cso")
set(GBUFFER_TASK_MESH_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GBuffer/GBuffer.task.ms.hlsl")
set(GBUFFER_TASK_MESH_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/GBuffer.task.ms.cso")

set(RAYTRACING_SHADER_MODEL ${DX12_RENDERER_RAYTRACING_SHADER_MODEL})

add_custom_command(TARGET ${TARGET_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
        COMMAND "${DXC_EXECUTABLE}" -T ${RAYTRACING_SHADER_MODEL} -HV 2021 -Fo "${RAYTRACING_SHADER_OUTPUT}" "${RAYTRACING_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/DXC/include" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_SOURCE_DIR}/External/NRD/Shaders" -I "${DX12_RENDERER_NRD_SHADER_INCLUDE_DIRECTORY}" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
        COMMAND "${DXC_EXECUTABLE}" -T ${RAYTRACING_SHADER_MODEL} -HV 2021 -Fo "${RAYTRACING_SOFT_SHADOW_SHADER_OUTPUT}" "${RAYTRACING_SOFT_SHADOW_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/DXC/include" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_SOURCE_DIR}/External/NRD/Shaders" -I "${DX12_RENDERER_NRD_SHADER_INCLUDE_DIRECTORY}" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
        COMMAND "${DXC_EXECUTABLE}" -E main -T as_${DX12_RENDERER_SHADER_MODEL_SUFFIX} -HV 2021 -Fo "${GBUFFER_TASK_SHADER_OUTPUT}" "${GBUFFER_TASK_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
        COMMAND "${DXC_EXECUTABLE}" -E main -T ms_${DX12_RENDERER_SHADER_MODEL_SUFFIX} -HV 2021 -Fo "${GBUFFER_TASK_MESH_SHADER_OUTPUT}" "${GBUFFER_TASK_MESH_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_SOURCE_DIR}/Framework/shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
        VERBATIM
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAYTRACING_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/PathTracing.rt.cso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAYTRACING_SOFT_SHADOW_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/PathTracing.softshadow.rt.cso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GBUFFER_TASK_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/GBuffer.task.as.cso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GBUFFER_TASK_MESH_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/GBuffer.task.ms.cso"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DX12_RENDERER_CUDA_BLOOM_PTX_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/CudaBloom.ptx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CUDA/CudaBloom.cu" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/CudaBloom.cu"
        VERBATIM
)
