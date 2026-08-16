cmake_minimum_required(VERSION 3.8.0)

project("Framework")

# Modify Begin:2026-07-30 by BestHui
# Framework C++ files are collected recursively so the source tree can stay organized by module.
# Modify End

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

# Modify Begin:2026-08-16 by BestHui
# ReSTIR shader implementations belong to Framework. They are listed here for
# IDE ownership; RaytracingDemo only compiles its scene-specific adapters.
set(FRAMEWORK_RESTIR_DI_SHADER_FILES
        "shaders/ReSTIRDI/ReSTIRDI.RIS.cs.hlsl"
        "shaders/ReSTIRDI/ReSTIRDI.Temporal.cs.hlsl"
        "shaders/ReSTIRDI/ReSTIRDI.Spatial.cs.hlsl"
        "shaders/ReSTIRDI/ReSTIRDI.Shade.cs.hlsl"
        )
set(FRAMEWORK_RESTIR_GI_SHADER_FILES
        "shaders/ReSTIRGI/ReSTIRGI.Initial.cs.hlsl"
        "shaders/ReSTIRGI/ReSTIRGI.Temporal.cs.hlsl"
        "shaders/ReSTIRGI/ReSTIRGI.Spatial.cs.hlsl"
        "shaders/ReSTIRGI/ReSTIRGI.Shade.cs.hlsl"
        )
set(FRAMEWORK_RESTIR_DI_ALL_SHADER_FILES
        ${FRAMEWORK_RESTIR_DI_SHADER_FILES}
        "shaders/ReSTIRDI/ReSTIRDI.hlsli"
        "shaders/ReSTIRDI/ReSTIRDIConstants.hlsli"
        "shaders/ReSTIRDI/ReSTIRDISceneContract.hlsli"
        )
set(FRAMEWORK_RESTIR_GI_ALL_SHADER_FILES
        ${FRAMEWORK_RESTIR_GI_SHADER_FILES}
        "shaders/ReSTIRGI/ReSTIRGI.hlsli"
        "shaders/ReSTIRGI/ReSTIRGIConstants.hlsli"
        "shaders/ReSTIRGI/ReSTIRGISceneContract.hlsli"
        )
# Modify End

# Modify Begin:2026-07-27 by BestHui
set(SHADERS_HEADER_FILES
# Modify Begin:2026-08-06 by BestHui
        "shaders/Common/Noise.hlsli"
        "shaders/Common/EnvironmentTexture.hlsli"
        "shaders/Bindless/BindlessResources.hlsli"
        # Modify Begin:2026-07-30 by BestHui
        "shaders/Lighting/MaterialEvaluation.hlsli"
        # Modify End
        "shaders/Lighting/SurfaceEmitter.hlsli"
# Modify End
        "shaders/SVGF/SVGFCommon.hlsli"
# Modify Begin:2026-07-31 by BestHui
        "shaders/Meshlet/MeshletCommon.hlsli"
# Modify End
        ${FRAMEWORK_RESTIR_DI_ALL_SHADER_FILES}
        ${FRAMEWORK_RESTIR_GI_ALL_SHADER_FILES}
        )
# Modify End

set(TARGET_NAME Framework)
set(SHADERS_OUTPUT_HEADERS TRUE)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)

# Modify Begin:2026-08-16 by BestHui
set_source_files_properties(
        ${FRAMEWORK_RESTIR_DI_SHADER_FILES}
        ${FRAMEWORK_RESTIR_GI_SHADER_FILES}
        PROPERTIES HEADER_FILE_ONLY TRUE)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/Framework" PREFIX "Include" FILES ${HEADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES ${SOURCE_FILES})
set(FRAMEWORK_GENERAL_SHADER_FILES ${SHADER_FILES} ${SHADERS_HEADER_FILES})
list(REMOVE_ITEM FRAMEWORK_GENERAL_SHADER_FILES ${FRAMEWORK_RESTIR_DI_ALL_SHADER_FILES})
list(REMOVE_ITEM FRAMEWORK_GENERAL_SHADER_FILES ${FRAMEWORK_RESTIR_GI_ALL_SHADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/shaders" PREFIX "Shaders" FILES ${FRAMEWORK_GENERAL_SHADER_FILES})
source_group("Shaders\\ReSTIR DI" FILES ${FRAMEWORK_RESTIR_DI_ALL_SHADER_FILES})
source_group("Shaders\\ReSTIR GI" FILES ${FRAMEWORK_RESTIR_GI_ALL_SHADER_FILES})
# Modify End

# Add source to this project's executable.
add_library(${TARGET_NAME} STATIC ${HEADER_FILES} ${SOURCE_FILES} ${SHADER_FILES} ${SHADERS_HEADER_FILES})

# Modify Begin:2026-08-16 by BestHui
# Dear ImGui is compiled into Framework so every ImGui::Slider* call shares the same double-click input behavior.
set(DX12_RENDERER_IMGUI_SOURCES
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui_draw.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui_tables.cpp"
        "${DX12_RENDERER_IMGUI_PATCHED_WIDGETS_SOURCE}"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/backends/imgui_impl_dx12.cpp"
        "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/backends/imgui_impl_win32.cpp"
        )
target_sources(${TARGET_NAME} PRIVATE ${DX12_RENDERER_IMGUI_SOURCES})
source_group("Third Party\\Dear ImGui" FILES ${DX12_RENDERER_IMGUI_SOURCES})
# Modify End

# Modify Begin:2026-07-28 by BestHui
target_compile_options(${TARGET_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
# Modify End

include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBasePost.cmake)

target_include_directories(${TARGET_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/Framework
# Modify Begin:2026-08-07 by BestHui
        PRIVATE "${DX12_RENDERER_DLSS_INCLUDE_DIRECTORY}"
        PRIVATE "${DX12_RENDERER_STREAMLINE_INCLUDE_DIRECTORY}"
# Modify End
# Modify Begin:2026-08-16 by BestHui
        PUBLIC "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}"
        PUBLIC "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/backends"
# Modify End
        )

# Modify Begin:2026-07-28 by BestHui
# Modify Begin:2026-07-30 by BestHui
set(DX12_RENDERER_CUDA_TOOLKIT_ROOT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8" CACHE PATH "CUDA Toolkit 12.8 root used by DX12 renderer CUDA interop.")

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

if (NOT DX12_RENDERER_CUDA_INCLUDE_DIR OR NOT DX12_RENDERER_CUDA_DRIVER_LIBRARY)
        message(FATAL_ERROR "Framework CUDA interop requires CUDA Toolkit 12.8 driver headers/libs. Set DX12_RENDERER_CUDA_TOOLKIT_ROOT to the installed v12.8 directory.")
endif()
# Modify End

target_include_directories(${TARGET_NAME}
        PUBLIC "${DX12_RENDERER_CUDA_INCLUDE_DIR}"
        PUBLIC "${CMAKE_SOURCE_DIR}/External/UnityPluginAPI"
        )
# Modify End

target_link_libraries(${TARGET_NAME}
        PUBLIC DX12Library
# Modify Begin:2026-08-07 by BestHui
        PUBLIC "${DX12_RENDERER_DLSS_LIBRARY}"
        PUBLIC "${DX12_RENDERER_STREAMLINE_LIBRARY}"
# Modify End
# Modify Begin:2026-07-28 by BestHui
        PUBLIC "${DX12_RENDERER_CUDA_DRIVER_LIBRARY}"
# Modify End
# Modify Begin:2026-08-10 by BestHui
        PUBLIC NRD
        PUBLIC NRDIntegration
        PUBLIC NRI
# Modify End
        )

# Modify Begin:2026-08-16 by BestHui
target_link_libraries(${TARGET_NAME} PRIVATE d3d12 dxgi dxguid imm32)
# Modify End

# Modify Begin:2026-07-30 by BestHui
find_package(meshoptimizer CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC meshoptimizer::meshoptimizer)
# Modify End

# Modify Begin:2026-08-16 by BestHui
option(DX12_RENDERER_BUILD_DEVELOPER_TOOLS "Build developer-only command-line tools." OFF)
if (DX12_RENDERER_BUILD_DEVELOPER_TOOLS)
        add_executable(UnitySceneDump "tools/UnitySceneDump.cpp")
        target_compile_options(UnitySceneDump PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
        target_compile_features(UnitySceneDump PRIVATE cxx_std_20)
        target_link_libraries(UnitySceneDump PRIVATE Framework)
        set_target_properties(UnitySceneDump PROPERTIES FOLDER "Tools")
endif()
# Modify End
