cmake_minimum_required(VERSION 3.8.0)

# Use solution folders to organize projects
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Make sure DLL and EXE targets go to the same directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Output directory for static lib (.LIB)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for shared lib (.DLL)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for executables (.EXE)

cmake_policy(SET CMP0079 NEW)

project(LearningDirectX12 LANGUAGES CXX)

# Modify Begin:2026-07-21 by BestHui
set(DX12_RENDERER_ENABLE_SM610_LINALG OFF)
set(DX12_RENDERER_RAYTRACING_SHADER "${CMAKE_SOURCE_DIR}/Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rt.hlsl")
if (EXISTS "${DX12_RENDERER_RAYTRACING_SHADER}")
        file(STRINGS "${DX12_RENDERER_RAYTRACING_SHADER}" DX12_RENDERER_LINALG_DEFINE_LINE
                REGEX "^[ \t]*#[ \t]*define[ \t]+RAYTRACING_DEMO_ENABLE_LINALG[ \t]+[01]"
                LIMIT_COUNT 1)
        if (DX12_RENDERER_LINALG_DEFINE_LINE MATCHES "^[ \t]*#[ \t]*define[ \t]+RAYTRACING_DEMO_ENABLE_LINALG[ \t]+1")
                set(DX12_RENDERER_ENABLE_SM610_LINALG ON)
        endif()
endif()
message(STATUS "RaytracingDemo SM6.10 LinAlg path: ${DX12_RENDERER_ENABLE_SM610_LINALG}")
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
