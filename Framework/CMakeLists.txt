cmake_minimum_required(VERSION 3.8.0)

project("Framework")

# Framework C++ files are collected recursively so the source tree can stay organized by module.

option(DX12_RENDERER_BUILD_DEVELOPER_TOOLS "Build developer-only command-line tools." OFF)
if (NOT DX12_RENDERER_BUILD_DEVELOPER_TOOLS)
        set(DX12_RENDERER_IDE_ONLY_FILES
                "tools/UnitySceneDump.cpp"
                )
        set_source_files_properties(${DX12_RENDERER_IDE_ONLY_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
endif ()

set(SHADER_FILES_VERTEX
        "shaders/Blit_VS.hlsl"
        )

set(SHADER_FILES_PIXEL
        "shaders/Blit_PS.hlsl"
        "shaders/Bloom_Prefilter_PS.hlsl"
        "shaders/Bloom_Downsample_PS.hlsl"
        "shaders/Bloom_Composite_PS.hlsl"
        "shaders/TAA_Resolve_PS.hlsl"
        "shaders/ImGuiCombine_PS.hlsl"
        )

set(SHADER_FILES_COMPUTE
        "shaders/MSAADepthResolve_CS.hlsl"
        "shaders/NRD/NRDInputAdapter_CS.hlsl"
        "shaders/NRD/NRDOutputComposite_CS.hlsl"
        "shaders/SVGF/SVGFAtrous_CS.hlsl"
        "shaders/SVGF/SVGFComposite_CS.hlsl"
        "shaders/SVGF/SVGFTemporal_CS.hlsl"
        )

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

set(SHADERS_HEADER_FILES
        "shaders/Common/Noise.hlsli"
        "shaders/Common/EnvironmentTexture.hlsli"
        "shaders/Bindless/BindlessResources.hlsli"
        "shaders/Lighting/MaterialEvaluation.hlsli"
        "shaders/Lighting/SurfaceEmitter.hlsli"
        "shaders/SVGF/SVGFCommon.hlsli"
        "shaders/Meshlet/MeshletCommon.hlsli"
        ${FRAMEWORK_RESTIR_DI_ALL_SHADER_FILES}
        ${FRAMEWORK_RESTIR_GI_ALL_SHADER_FILES}
        )

set(TARGET_NAME Framework)
set(SHADERS_OUTPUT_HEADERS TRUE)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)

set_source_files_properties(
        ${FRAMEWORK_RESTIR_DI_SHADER_FILES}
        ${FRAMEWORK_RESTIR_GI_SHADER_FILES}
        PROPERTIES HEADER_FILE_ONLY TRUE)

# Add source to this project's executable.
add_library(${TARGET_NAME} STATIC
        ${HEADER_FILES}
        ${SOURCE_FILES}
        ${SHADER_FILES}
        ${SHADERS_HEADER_FILES}
        ${DX12_RENDERER_IDE_ONLY_FILES})

# Dear ImGui is compiled into Framework so every ImGui::Slider* call shares the same double-click input behavior.
set(DX12_RENDERER_IMGUI_SOURCES
        ${DX12_RENDERER_IMGUI_COMPILED_SOURCES}
        )
target_sources(${TARGET_NAME} PRIVATE ${DX12_RENDERER_IMGUI_SOURCES})
if (CMAKE_GENERATOR MATCHES "^Visual Studio")
        set_property(SOURCE ${DX12_RENDERER_IMGUI_SOURCES} APPEND PROPERTY
                VS_SETTINGS "Visible=false")
endif ()

target_compile_options(${TARGET_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBasePost.cmake)

target_include_directories(${TARGET_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/Framework
        PRIVATE "${DX12_RENDERER_DLSS_INCLUDE_DIRECTORY}"
        PRIVATE "${DX12_RENDERER_STREAMLINE_INCLUDE_DIRECTORY}"
        PUBLIC "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}"
        PUBLIC "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/backends"
        )

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
        message(FATAL_ERROR "Framework CUDA interop requires CUDA Toolkit 12.8 driver headers and library. Set DX12_RENDERER_CUDA_TOOLKIT_ROOT to the installed v12.8 directory.")
endif()

target_include_directories(${TARGET_NAME}
        PUBLIC "${DX12_RENDERER_CUDA_INCLUDE_DIR}"
        PUBLIC "${CMAKE_SOURCE_DIR}/External/UnityPluginAPI"
        )

target_link_libraries(${TARGET_NAME}
        PUBLIC DX12Library
        PUBLIC "${DX12_RENDERER_DLSS_LIBRARY}"
        PUBLIC "${DX12_RENDERER_STREAMLINE_LIBRARY}"
        PUBLIC "${DX12_RENDERER_CUDA_DRIVER_LIBRARY}"
        PUBLIC NRD
        PUBLIC NRDIntegration
        PUBLIC NRI
        )

target_link_libraries(${TARGET_NAME} PRIVATE d3d12 dxgi dxguid imm32)

find_package(meshoptimizer CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC meshoptimizer::meshoptimizer)

if (DX12_RENDERER_BUILD_DEVELOPER_TOOLS)
        add_executable(UnitySceneDump "tools/UnitySceneDump.cpp")
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES "tools/UnitySceneDump.cpp")
        target_compile_options(UnitySceneDump PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
        target_compile_features(UnitySceneDump PRIVATE cxx_std_20)
        target_link_libraries(UnitySceneDump PRIVATE Framework)
        set_target_properties(UnitySceneDump PROPERTIES FOLDER "Tools")
endif()
