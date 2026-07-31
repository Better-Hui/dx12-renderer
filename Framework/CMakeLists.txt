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

# Modify Begin:2026-07-27 by BestHui
set(SHADERS_HEADER_FILES
        "shaders/SVGF/SVGFCommon.hlsli"
# Modify Begin:2026-07-31 by BestHui
        "shaders/Meshlet/MeshletCommon.hlsli"
# Modify End
        )
# Modify End

set(TARGET_NAME Framework)
set(SHADERS_OUTPUT_HEADERS TRUE)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)

# Modify Begin:2026-07-30 by BestHui
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/Framework" PREFIX "include" FILES ${HEADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${SOURCE_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/shaders" PREFIX "shaders" FILES ${SHADERS_HEADER_FILES})
# Modify End

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

# Modify Begin:2026-07-30 by BestHui
find_package(meshoptimizer CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC meshoptimizer::meshoptimizer)
# Modify End

# Modify Begin:2026-07-29 by BestHui
add_executable(UnitySceneDump "tools/UnitySceneDump.cpp")
target_compile_options(UnitySceneDump PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_compile_features(UnitySceneDump PRIVATE cxx_std_20)
target_link_libraries(UnitySceneDump PRIVATE Framework)
set_target_properties(UnitySceneDump PROPERTIES FOLDER "Framework/Tools")
# Modify End
