set(DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY "${CMAKE_BINARY_DIR}/ThirdParty")

# Vendored implementations are built in CMakeIncludes/ThirdPartyBuild and
# exposed here only as imported targets. This keeps the main solution focused
# on renderer-owned projects while retaining source-based provisioning.
set(DX12_RENDERER_EXTERNAL_LIBRARY_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin")
set(DX12_RENDERER_EXTERNAL_RUNTIME_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin")
set(DX12_RENDERER_EXTERNAL_IMATH_INCLUDE_DIRECTORY
        "${CMAKE_SOURCE_DIR}/External/Imath/src/Imath;${CMAKE_SOURCE_DIR}/External/Imath/src;${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/Imath/config")
set(DX12_RENDERER_EXTERNAL_OPENEXR_INCLUDE_DIRECTORY
        "${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/OpenEXR;${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/OpenEXR_build_interface_include;${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/OpenEXR/cmake;${DX12_RENDERER_EXTERNAL_IMATH_INCLUDE_DIRECTORY};${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/OpenEXR/OpenEXR_ImathIncludeCompat;${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/Iex;${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/IlmThread;${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/OpenEXRCore")
file(MAKE_DIRECTORY
        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/Imath/config"
        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/OpenEXR_build_interface_include"
        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/OpenEXR/cmake"
        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/OpenEXR/OpenEXR_ImathIncludeCompat")

function(dx12_renderer_define_external_library target_name release_name debug_name include_directory)
        add_library(${target_name} SHARED IMPORTED GLOBAL)
        set_target_properties(${target_name} PROPERTIES
                IMPORTED_CONFIGURATIONS "Debug;Release"
                IMPORTED_IMPLIB_DEBUG "${DX12_RENDERER_EXTERNAL_LIBRARY_DIRECTORY}/Debug/${debug_name}.lib"
                IMPORTED_LOCATION_DEBUG "${DX12_RENDERER_EXTERNAL_RUNTIME_DIRECTORY}/Debug/${debug_name}.dll"
                IMPORTED_IMPLIB_RELEASE "${DX12_RENDERER_EXTERNAL_LIBRARY_DIRECTORY}/Release/${release_name}.lib"
                IMPORTED_LOCATION_RELEASE "${DX12_RENDERER_EXTERNAL_RUNTIME_DIRECTORY}/Release/${release_name}.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${include_directory}")
endfunction()

function(dx12_renderer_define_external_library_with_archive_directory target_name release_name debug_name include_directory archive_directory)
        add_library(${target_name} SHARED IMPORTED GLOBAL)
        set_target_properties(${target_name} PROPERTIES
                IMPORTED_CONFIGURATIONS "Debug;Release"
                IMPORTED_IMPLIB_DEBUG "${archive_directory}/Debug/${debug_name}.lib"
                IMPORTED_LOCATION_DEBUG "${DX12_RENDERER_EXTERNAL_RUNTIME_DIRECTORY}/Debug/${debug_name}.dll"
                IMPORTED_IMPLIB_RELEASE "${archive_directory}/Release/${release_name}.lib"
                IMPORTED_LOCATION_RELEASE "${DX12_RENDERER_EXTERNAL_RUNTIME_DIRECTORY}/Release/${release_name}.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${include_directory}")
endfunction()

dx12_renderer_define_external_library(meshoptimizer::meshoptimizer
        meshoptimizer meshoptimizerd "${CMAKE_SOURCE_DIR}/External/meshoptimizer/src")
dx12_renderer_define_external_library(assimp::assimp
        assimp-vc143-mt assimp-vc143-mtd
        "${CMAKE_SOURCE_DIR}/External/assimp/include;${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/assimp/include")
dx12_renderer_define_external_library_with_archive_directory(Microsoft::DirectXTex
        DirectXTex DirectXTexd "${CMAKE_SOURCE_DIR}/External/DirectXTex/DirectXTex"
        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/lib")
dx12_renderer_define_external_library_with_archive_directory(Microsoft::DirectXMesh
        DirectXMesh DirectXMeshd "${CMAKE_SOURCE_DIR}/External/DirectXMesh/DirectXMesh"
        "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/lib")
dx12_renderer_define_external_library(Imath::Imath
        Imath-3_2 Imath-3_2d "${DX12_RENDERER_EXTERNAL_IMATH_INCLUDE_DIRECTORY}")
dx12_renderer_define_external_library(OpenEXR::OpenEXR
        OpenEXR-4_0 OpenEXR-4_0d "${DX12_RENDERER_EXTERNAL_OPENEXR_INCLUDE_DIRECTORY}")
dx12_renderer_define_external_library(OpenEXR::OpenEXRCore
        OpenEXRCore-4_0 OpenEXRCore-4_0d "${DX12_RENDERER_EXTERNAL_OPENEXR_INCLUDE_DIRECTORY}")
dx12_renderer_define_external_library(OpenEXR::Iex
        Iex-4_0 Iex-4_0d "${DX12_RENDERER_EXTERNAL_OPENEXR_INCLUDE_DIRECTORY}")
dx12_renderer_define_external_library(OpenEXR::IlmThread
        IlmThread-4_0 IlmThread-4_0d "${DX12_RENDERER_EXTERNAL_OPENEXR_INCLUDE_DIRECTORY}")

add_library(DX12Renderer::zlibstatic STATIC IMPORTED GLOBAL)
set_target_properties(DX12Renderer::zlibstatic PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG "${DX12_RENDERER_EXTERNAL_LIBRARY_DIRECTORY}/Debug/zlibstaticd.lib"
        IMPORTED_LOCATION_RELEASE "${DX12_RENDERER_EXTERNAL_LIBRARY_DIRECTORY}/Release/zlibstatic.lib")
set_target_properties(assimp::assimp PROPERTIES
        INTERFACE_LINK_LIBRARIES "DX12Renderer::zlibstatic")
set_target_properties(OpenEXR::OpenEXR PROPERTIES
        INTERFACE_LINK_LIBRARIES "OpenEXR::OpenEXRCore;OpenEXR::Iex;OpenEXR::IlmThread;Imath::Imath")
set_target_properties(OpenEXR::OpenEXRCore PROPERTIES
        INTERFACE_LINK_LIBRARIES "Imath::Imath")
set(DX12_RENDERER_NRD_SHADER_INCLUDE_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/NRD/Shaders")
set(DX12_RENDERER_OIDN_SOURCE_DIRECTORY "${CMAKE_SOURCE_DIR}/External/OIDN")
set(DX12_RENDERER_OIDN_BUILD_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/OIDN")
set(DX12_RENDERER_OIDN_GENERATED_INCLUDE_DIRECTORY "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/include/OpenImageDenoise")
set(DX12_RENDERER_VCPKG_ROOT "" CACHE PATH "Optional vcpkg root used to locate oneTBB for the isolated OIDN build.")
if (NOT DX12_RENDERER_VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
        file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" DX12_RENDERER_VCPKG_ROOT)
endif()
set(DX12_RENDERER_OIDN_TBB_DEFAULT_ROOT "${CMAKE_BINARY_DIR}/ThirdParty/oneTBB-install")
if (DX12_RENDERER_VCPKG_ROOT AND EXISTS "${DX12_RENDERER_VCPKG_ROOT}/installed/x64-windows/share/tbb/TBBConfig.cmake")
        set(DX12_RENDERER_OIDN_TBB_DEFAULT_ROOT "${DX12_RENDERER_VCPKG_ROOT}/installed/x64-windows")
endif()
set(DX12_RENDERER_OIDN_TBB_CONFIG_DEFAULT_DIRECTORY "${DX12_RENDERER_OIDN_TBB_DEFAULT_ROOT}/lib/cmake/tbb")
if (DX12_RENDERER_VCPKG_ROOT AND EXISTS "${DX12_RENDERER_VCPKG_ROOT}/installed/x64-windows/share/tbb/TBBConfig.cmake")
        set(DX12_RENDERER_OIDN_TBB_CONFIG_DEFAULT_DIRECTORY "${DX12_RENDERER_OIDN_TBB_DEFAULT_ROOT}/share/tbb")
endif()
set(DX12_RENDERER_OIDN_TBB_ROOT "${DX12_RENDERER_OIDN_TBB_DEFAULT_ROOT}" CACHE PATH "oneTBB installation root used by the isolated OIDN build." FORCE)
set(DX12_RENDERER_OIDN_TBB_CONFIG_DIRECTORY "${DX12_RENDERER_OIDN_TBB_CONFIG_DEFAULT_DIRECTORY}" CACHE PATH "oneTBB CMake package directory used by the isolated OIDN build." FORCE)
set(DX12_RENDERER_OIDN_TBB_RUNTIME_ROOT "${DX12_RENDERER_OIDN_TBB_DEFAULT_ROOT}" CACHE PATH "oneTBB runtime root deployed with OIDN." FORCE)
set(DX12_RENDERER_OIDN_ISPC_DEFAULT_EXECUTABLE "${CMAKE_SOURCE_DIR}/External/ispc-v1.30.0-windows/bin/ispc.exe")
if (NOT EXISTS "${DX12_RENDERER_OIDN_ISPC_DEFAULT_EXECUTABLE}")
        set(DX12_RENDERER_OIDN_ISPC_DEFAULT_EXECUTABLE "${CMAKE_BINARY_DIR}/ThirdPartyTools/ispc-v1.30.0-windows/bin/ispc.exe")
endif()
set(DX12_RENDERER_OIDN_ISPC_EXECUTABLE "${DX12_RENDERER_OIDN_ISPC_DEFAULT_EXECUTABLE}" CACHE FILEPATH "ISPC executable used by the isolated OIDN build.")
file(MAKE_DIRECTORY "${DX12_RENDERER_OIDN_GENERATED_INCLUDE_DIRECTORY}")

function(dx12_renderer_define_imported_library target_name include_directory)
        add_library(${target_name} SHARED IMPORTED GLOBAL)
        set_target_properties(${target_name} PROPERTIES
                IMPORTED_CONFIGURATIONS "Debug;Release"
                IMPORTED_IMPLIB_DEBUG "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Debug/${target_name}.lib"
                IMPORTED_LOCATION_DEBUG "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Debug/${target_name}.dll"
                IMPORTED_IMPLIB_RELEASE "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Release/${target_name}.lib"
                IMPORTED_LOCATION_RELEASE "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin/Release/${target_name}.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${include_directory}"
                )
endfunction()

dx12_renderer_define_imported_library(NRI "${CMAKE_SOURCE_DIR}/External/NRI/Include")
dx12_renderer_define_imported_library(NRD "${CMAKE_SOURCE_DIR}/External/NRD/Include")
set_target_properties(NRI PROPERTIES INTERFACE_COMPILE_DEFINITIONS "NRI_STATIC_LIBRARY=0")
set_target_properties(NRD PROPERTIES INTERFACE_COMPILE_DEFINITIONS "NRD_STATIC_LIBRARY=0")

add_library(OIDN SHARED IMPORTED GLOBAL)
set_target_properties(OIDN PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_IMPLIB_DEBUG "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/Debug/OpenImageDenoise.lib"
        IMPORTED_LOCATION_DEBUG "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/Debug/OpenImageDenoise.dll"
        IMPORTED_IMPLIB_RELEASE "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/Release/OpenImageDenoise.lib"
        IMPORTED_LOCATION_RELEASE "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/Release/OpenImageDenoise.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${DX12_RENDERER_OIDN_SOURCE_DIRECTORY}/include;${DX12_RENDERER_OIDN_GENERATED_INCLUDE_DIRECTORY}"
        )
set(DX12_RENDERER_OIDN_RUNTIME_FILES
        "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/$<CONFIG>/OpenImageDenoise.dll"
        "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/$<CONFIG>/OpenImageDenoise_core.dll"
        "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/$<CONFIG>/OpenImageDenoise_device_cpu.dll"
        "${DX12_RENDERER_OIDN_BUILD_DIRECTORY}/$<CONFIG>/OpenImageDenoise_device_cuda.dll"
        "${DX12_RENDERER_OIDN_TBB_RUNTIME_ROOT}/bin/tbb12.dll"
        )

set(DX12_RENDERER_EXTERNAL_RUNTIME_FILES
        "$<TARGET_FILE:meshoptimizer::meshoptimizer>"
        "$<TARGET_FILE:assimp::assimp>"
        "$<TARGET_FILE:Microsoft::DirectXTex>"
        "$<TARGET_FILE:Microsoft::DirectXMesh>"
        "$<TARGET_FILE:Imath::Imath>"
        "$<TARGET_FILE:OpenEXR::OpenEXR>"
        "$<TARGET_FILE:OpenEXR::OpenEXRCore>"
        "$<TARGET_FILE:OpenEXR::Iex>"
        "$<TARGET_FILE:OpenEXR::IlmThread>")

add_library(NRDIntegration INTERFACE IMPORTED GLOBAL)
set_target_properties(NRDIntegration PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/External/NRD/Integration"
        )

function(dx12_renderer_add_third_party_prebuild target_name)
        # Keep the third-party refresh private to its consuming target. A custom
        # target would otherwise materialize as a developer-visible VS project.
        add_custom_command(TARGET ${target_name} PRE_BUILD
                COMMAND "${CMAKE_COMMAND}" "-DDX12_RENDERER_SOURCE_ROOT=${CMAKE_SOURCE_DIR}" "-DDX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY=${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}" "-DDX12_RENDERER_CONFIGURATION=$<CONFIG>" "-DDX12_RENDERER_GENERATOR=${CMAKE_GENERATOR}" "-DDX12_RENDERER_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}" "-DDX12_RENDERER_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}" "-DDX12_RENDERER_DXC_EXECUTABLE=${DX12_RENDERER_DXC_EXECUTABLE}" "-DDX12_RENDERER_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" -P "${CMAKE_SOURCE_DIR}/CMakeIncludes/BuildThirdParty.cmake"
                COMMENT "Updating isolated NRI and NRD dependencies"
                VERBATIM)
endfunction()

function(dx12_renderer_add_oidn_prebuild target_name)
        add_custom_command(TARGET ${target_name} PRE_BUILD
                COMMAND "${CMAKE_COMMAND}" "-DDX12_RENDERER_SOURCE_ROOT=${CMAKE_SOURCE_DIR}" "-DDX12_RENDERER_OIDN_BUILD_DIRECTORY=${DX12_RENDERER_OIDN_BUILD_DIRECTORY}" "-DDX12_RENDERER_CONFIGURATION=$<CONFIG>" "-DDX12_RENDERER_GENERATOR=${CMAKE_GENERATOR}" "-DDX12_RENDERER_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}" "-DDX12_RENDERER_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}" "-DDX12_RENDERER_OIDN_TBB_ROOT=${DX12_RENDERER_OIDN_TBB_ROOT}" "-DDX12_RENDERER_OIDN_TBB_CONFIG_DIRECTORY=${DX12_RENDERER_OIDN_TBB_CONFIG_DIRECTORY}" "-DDX12_RENDERER_OIDN_ISPC_EXECUTABLE=${DX12_RENDERER_OIDN_ISPC_EXECUTABLE}" "-DDX12_RENDERER_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" -P "${CMAKE_SOURCE_DIR}/CMakeIncludes/BuildOidn.cmake"
                COMMENT "Updating isolated Open Image Denoise CPU and CUDA dependencies"
                VERBATIM)
endfunction()

function(dx12_renderer_add_external_runtime_copy target_name)
        add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${DX12_RENDERER_EXTERNAL_RUNTIME_FILES}
                        "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM)
endfunction()
