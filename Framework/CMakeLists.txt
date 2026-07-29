cmake_minimum_required(VERSION 3.8.0)

project("Framework")

set(HEADER_FILES
        "include/Framework/Aabb.h"
        "include/Framework/Armature.h"
        
        "include/Framework/Bone.h"
        "include/Framework/BoundingSphere.h"
        
        "include/Framework/Mesh.h"
        "include/Framework/Model.h"
        
        "include/Framework/GameObject.h"
        "include/Framework/Light.h"
        "include/Framework/MatricesCb.h"
        "include/Framework/ModelLoader.h"
        "include/Framework/Animation.h"
        "include/Framework/GraphicsSettings.h"
        "include/Framework/DemoMain.h"
        "include/Framework/Bloom.h"
        "include/Framework/BloomPrefilter.h"
        "include/Framework/BloomDownsample.h"
        "include/Framework/BloomUpsample.h" 
        "include/Framework/CommandContext.h"
# Modify Begin:2026-07-29 by BestHui
        "include/Framework/CommandContextDescriptorAllocator.h"
# Modify End
# Modify Begin:2026-07-28 by BestHui
        "include/Framework/CudaInterop.h"
# Modify End
        "include/Framework/ComputePipelineStateBuilder.h"
        "include/Framework/DescriptorLayout.h"
# Modify Begin:2026-07-24 by BestHui
        "include/Framework/PipelineBindingSet.h"
        "include/Framework/PipelineDescriptorPool.h"
        "include/Framework/PipelineDescriptorSet.h"
        "include/Framework/PipelineLayout.h"
        "include/Framework/PipelineStateCache.h"
        "include/Framework/PipelineStateKey.h"
# Modify End
        "include/Framework/FrameworkDeprecated.h"
        "include/Framework/RasterPipelineStateBuilder.h" 
        "include/Framework/Shader.h" 
        "include/Framework/Material.h" 
        "include/Framework/ShaderResourceView.h"
        "include/Framework/ShaderVariant.h"
        "include/Framework/ShaderBlob.h"
        "include/Framework/ShaderReflection.h"
        "include/Framework/SharedUploadBuffer.h"
        "include/Framework/UnorderedAccessView.h"
        # Modify Begin:2026-07-28 by BestHui
        "include/Framework/UnityD3D12Interop.h"
        # Modify End
# Modify Begin:2026-07-29 by BestHui
        "include/Framework/UnitySceneParser.h"
# Modify End
        "include/Framework/ComputeShader.h"
# Modify Begin:2026-07-23 by BestHui
        "include/Framework/RenderTexture.h"
# Modify End
# Modify Begin:2026-07-21 by BestHui
        "include/Framework/RayTracingAccelerationStructure.h"
        "include/Framework/RayTracingPipelineStateBuilder.h"
        "include/Framework/RayTracingShader.h"
        "include/Framework/RayTracingShaderTable.h"
# Modify End
        "include/Framework/MSAADepthResolvePass.h"
# Modify Begin:2026-07-27 by BestHui
        "include/Framework/NRD.h"
        "include/Framework/SVGF.h"
# Modify End
        "include/Framework/SSAOUtils.h"
        "include/Framework/TAA.h"
        
        "include/Framework/ImGuiImpl.h"
)

set(SOURCE_FILES
        "src/Aabb.cpp"
        "src/Armature.cpp"
        
        "src/Bone.cpp"
        "src/BoundingSphere.cpp"
        "src/Framework.cpp"
        "src/Mesh.cpp"
        "src/Model.cpp"
        
        "src/GameObject.cpp"
        "src/Light.cpp"
        "src/MatricesCb.cpp"
        "src/ModelLoader.cpp"
        "src/Animation.cpp"
        "src/Bloom.cpp"
        "src/BloomPrefilter.cpp"
        "src/BloomDownsample.cpp"
        "src/BloomUpsample.cpp" 
        "src/CommandContext.cpp"
# Modify Begin:2026-07-29 by BestHui
        "src/CommandContextDescriptorAllocator.cpp"
# Modify End
# Modify Begin:2026-07-28 by BestHui
        "src/CudaInterop.cpp"
# Modify End
        "src/ComputePipelineStateBuilder.cpp"
        "src/DescriptorLayout.cpp"
# Modify Begin:2026-07-24 by BestHui
        "src/PipelineBindingSet.cpp"
        "src/PipelineDescriptorPool.cpp"
        "src/PipelineDescriptorSet.cpp"
        "src/PipelineLayout.cpp"
# Modify End
        "src/RasterPipelineStateBuilder.cpp" 
        "src/Shader.cpp" 
        "src/SharedUploadBuffer.cpp" 
        "src/Material.cpp" 
        "src/ShaderBlob.cpp"
        "src/ShaderReflection.cpp"
        "src/ShaderVariant.cpp"
        # Modify Begin:2026-07-28 by BestHui
        "src/UnityD3D12Interop.cpp"
        # Modify End
# Modify Begin:2026-07-29 by BestHui
        "src/UnitySceneParser.cpp"
# Modify End
        "src/ComputeShader.cpp"
# Modify Begin:2026-07-23 by BestHui
        "src/RenderTexture.cpp"
# Modify End
# Modify Begin:2026-07-21 by BestHui
        "src/RayTracingAccelerationStructure.cpp"
        "src/RayTracingBindingSet.cpp"
        "src/RayTracingDispatchTables.cpp"
        "src/RayTracingDispatchTables.h"
        "src/RayTracingPipelineStateBuilder.cpp"
        "src/RayTracingPipelineDescBuilder.cpp"
        "src/RayTracingShader.cpp"
        "src/RayTracingShaderInternal.h"
        "src/RayTracingShaderTable.cpp"
# Modify End
        "src/MSAADepthResolvePass.cpp"
# Modify Begin:2026-07-27 by BestHui
        "src/NRD.cpp"
        "src/SVGF.cpp"
# Modify End
        "src/SSAOUtils.cpp"
        "src/TAA.cpp"
        
        "src/ImGuiImpl.cpp"
        )

set(SHADER_FILES_VERTEX
        "shaders/Blit_VS.hlsl"
        )

set(SHADER_FILES_PIXEL
        "shaders/Blit_PS.hlsl"
        "shaders/Bloom_Prefilter_PS.hlsl"
        "shaders/Bloom_Downsample_PS.hlsl"
        "shaders/TAA_Resolve_PS.hlsl"
        "shaders/ImGuiCombine_PS.hlsl"
        )

set(SHADER_FILES_COMPUTE
        "shaders/MSAADepthResolve_CS.hlsl"
# Modify Begin:2026-07-27 by BestHui
        "shaders/NRD/NRDInputAdapter_CS.hlsl"
        "shaders/NRD/NRDOutputComposite_CS.hlsl"
        "shaders/SVGF/SVGFAtrous_CS.hlsl"
        "shaders/SVGF/SVGFComposite_CS.hlsl"
        "shaders/SVGF/SVGFTemporal_CS.hlsl"
# Modify End
        )

# Modify Begin:2026-07-27 by BestHui
set(SHADERS_HEADER_FILES
        "shaders/SVGF/SVGFCommon.hlsli"
        )
# Modify End

set(TARGET_NAME Framework)
set(SHADERS_OUTPUT_HEADERS TRUE)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)

# Add source to this project's executable.
add_library(${TARGET_NAME} STATIC ${HEADER_FILES} ${SOURCE_FILES} ${SHADER_FILES} ${SHADERS_HEADER_FILES})

# Modify Begin:2026-07-28 by BestHui
target_compile_options(${TARGET_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
# Modify End

include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBasePost.cmake)

target_include_directories(${TARGET_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/Framework
        )

# Modify Begin:2026-07-28 by BestHui
find_path(CUDA_INCLUDE_DIR
        NAMES cuda.h
        HINTS "$ENV{CUDA_PATH}/include" "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/include"
        )

find_library(CUDA_DRIVER_LIBRARY
        NAMES cuda
        HINTS "$ENV{CUDA_PATH}/lib/x64" "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/lib/x64"
        )

if (NOT CUDA_INCLUDE_DIR OR NOT CUDA_DRIVER_LIBRARY)
        message(FATAL_ERROR "Framework CUDA interop requires CUDA Toolkit driver headers/libs.")
endif()

target_include_directories(${TARGET_NAME}
        PUBLIC "${CUDA_INCLUDE_DIR}"
        PUBLIC "${CMAKE_SOURCE_DIR}/External/UnityPluginAPI"
        )
# Modify End

# Modify Begin:2026-07-27 by BestHui
set(NRD_ROOT "${CMAKE_SOURCE_DIR}/External/NRD")
set(NRI_ROOT "${CMAKE_SOURCE_DIR}/External/NRI")
set(NRD_BIN_DIR "${NRD_ROOT}/_Bin")
set(NRI_BIN_DIR "${NRD_ROOT}/_Bin")

target_include_directories(${TARGET_NAME}
        PUBLIC "${NRD_ROOT}/Include"
        PUBLIC "${NRD_ROOT}/Integration"
        PUBLIC "${NRI_ROOT}/Include"
        )
# Modify End

target_link_libraries(${TARGET_NAME}
        PUBLIC DX12Library
# Modify Begin:2026-07-28 by BestHui
        PUBLIC "${CUDA_DRIVER_LIBRARY}"
# Modify End
# Modify Begin:2026-07-27 by BestHui
        PUBLIC "${NRD_BIN_DIR}/NRD.lib"
        PUBLIC "${NRI_BIN_DIR}/NRI.lib"
# Modify End
        )
        
# imgui
find_package(imgui CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC imgui::imgui)

# Modify Begin:2026-07-29 by BestHui
add_executable(UnitySceneDump "tools/UnitySceneDump.cpp")
target_compile_options(UnitySceneDump PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_compile_features(UnitySceneDump PRIVATE cxx_std_20)
target_link_libraries(UnitySceneDump PRIVATE Framework)
set_target_properties(UnitySceneDump PROPERTIES FOLDER "Framework/Tools")
# Modify End
