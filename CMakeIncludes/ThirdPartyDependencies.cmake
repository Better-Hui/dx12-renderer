set(DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY "${CMAKE_BINARY_DIR}/ThirdParty")

# Vendored implementations are built in CMakeIncludes/ThirdPartyBuild and
# exposed here only as imported targets. This keeps the main solution focused
# on renderer-owned projects while retaining source-based provisioning.
set(DX12_RENDERER_EXTERNAL_LIBRARY_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin")
set(DX12_RENDERER_EXTERNAL_RUNTIME_DIRECTORY "${DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY}/bin")
set(DX12_RENDERER_ASSIMP_GENERATED_INCLUDE_DIRECTORY "${CMAKE_BINARY_DIR}/Generated/Assimp")
set(ASSIMP_DOUBLE_PRECISION OFF)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/assimp/include/assimp/config.h.in"
        "${DX12_RENDERER_ASSIMP_GENERATED_INCLUDE_DIRECTORY}/assimp/config.h"
        @ONLY)

set(DX12_RENDERER_IMATH_GENERATED_INCLUDE_DIRECTORY "${CMAKE_BINARY_DIR}/Generated/Imath")
set(IMATH_HALF_USE_LOOKUP_TABLE ON)
set(IMATH_HAVE_LARGE_STACK OFF)
set(IMATH_NAMESPACE_CUSTOM 0)
set(IMATH_INTERNAL_NAMESPACE Imath)
set(IMATH_NAMESPACE Imath)
set(IMATH_VERSION 3.2.0)
set(IMATH_PACKAGE_NAME "Imath 3.2.0-dev")
set(Imath_VERSION_MAJOR 3)
set(Imath_VERSION_MINOR 2)
set(Imath_VERSION_PATCH 0)
set(IMATH_VERSION_RELEASE_TYPE "-dev")
set(IMATH_LIB_VERSION "30.3.2.0")
set(IMATH_USE_NOEXCEPT ON)
set(IMATH_ENABLE_API_VISIBILITY ON)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/Imath/config/ImathConfig.h.in"
        "${DX12_RENDERER_IMATH_GENERATED_INCLUDE_DIRECTORY}/ImathConfig.h"
        @ONLY)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/Imath/config/ImathConfig.h.in"
        "${DX12_RENDERER_IMATH_GENERATED_INCLUDE_DIRECTORY}/Imath/ImathConfig.h"
        @ONLY)

set(DX12_RENDERER_OPENEXR_GENERATED_INCLUDE_DIRECTORY "${CMAKE_BINARY_DIR}/Generated/OpenEXR")
set(OPENEXR_VERSION_MAJOR 4)
set(OPENEXR_VERSION_MINOR 0)
set(OPENEXR_VERSION_PATCH 0)
set(OPENEXR_VERSION 4.0.0)
set(OPENEXR_SOVERSION 99)
set(OPENEXR_LIB_SOVERSION 99)
set(OPENEXR_LIB_VERSION "99.4.0.0")
set(OPENEXR_VERSION_RELEASE_TYPE "-dev")
set(OPENEXR_PACKAGE_NAME "OpenEXR 4.0.0-dev")
set(OPENEXR_NAMESPACE_CUSTOM 0)
set(OPENEXR_INTERNAL_IMF_NAMESPACE Imf_4_0)
set(OPENEXR_IMF_NAMESPACE Imf)
set(OPENEXR_HAVE_LARGE_STACK OFF)
set(OPENEXR_ENABLE_API_VISIBILITY ON)
set(OPENEXR_CORE_FUNCTIONS_EMBEDDED OFF)
set(OPENEXR_IMATH_SOVERSION 30)
set(OPENEXR_IMATH_VERSION_MAJOR 3)
set(OPENEXR_IMATH_VERSION_MINOR 2)
set(OPENEXR_IMATH_VERSION_PATCH 0)
set(Imath_SOVERSION 30)
set(Imath_VERSION_MAJOR 3)
set(Imath_VERSION_MINOR 2)
set(Imath_VERSION_PATCH 0)
set(openjph_VERSION_MAJOR 0)
set(openjph_VERSION_MINOR 32)
set(openjph_VERSION_PATCH 0)
set(zstd_VERSION_MAJOR 1)
set(zstd_VERSION_MINOR 5)
set(zstd_VERSION_PATCH 7)
set(OPENEXR_USE_INTERNAL_DEFLATE ON)
set(OPENEXR_USE_INTERNAL_ZSTD ON)
set(OPENEXR_IMF_HAVE_LINUX_PROCFS OFF)
set(OPENEXR_IMF_HAVE_DARWIN OFF)
set(OPENEXR_IMF_HAVE_COMPLETE_IOMANIP ON)
set(OPENEXR_IMF_HAVE_SYSCONF_NPROCESSORS_ONLN OFF)
set(OPENEXR_IMF_HAVE_GCC_INLINE_ASM_AVX OFF)
set(OPENEXR_MISSING_ARM_VLD1 OFF)
set(IEX_NAMESPACE_CUSTOM 0)
set(IEX_INTERNAL_NAMESPACE Iex_4_0)
set(IEX_NAMESPACE Iex)
set(ILMTHREAD_THREADING_ENABLED ON)
set(ILMTHREAD_HAVE_POSIX_SEMAPHORES OFF)
set(ILMTHREAD_USE_TBB OFF)
set(ILMTHREAD_NAMESPACE_CUSTOM 0)
set(ILMTHREAD_INTERNAL_NAMESPACE IlmThread_4_0)
set(ILMTHREAD_NAMESPACE IlmThread)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/OpenEXR/cmake/OpenEXRConfig.h.in"
        "${DX12_RENDERER_OPENEXR_GENERATED_INCLUDE_DIRECTORY}/OpenEXRConfig.h"
        @ONLY)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/OpenEXR/cmake/OpenEXRConfigInternal.h.in"
        "${DX12_RENDERER_OPENEXR_GENERATED_INCLUDE_DIRECTORY}/OpenEXRConfigInternal.h"
        @ONLY)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/OpenEXR/cmake/IexConfig.h.in"
        "${DX12_RENDERER_OPENEXR_GENERATED_INCLUDE_DIRECTORY}/IexConfig.h"
        @ONLY)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/OpenEXR/cmake/IlmThreadConfig.h.in"
        "${DX12_RENDERER_OPENEXR_GENERATED_INCLUDE_DIRECTORY}/IlmThreadConfig.h"
        @ONLY)
configure_file(
        "${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/OpenEXRCore/openexr_version.h"
        "${DX12_RENDERER_OPENEXR_GENERATED_INCLUDE_DIRECTORY}/OpenEXRCore/openexr_version.h"
        COPYONLY)
set(DX12_RENDERER_EXTERNAL_IMATH_INCLUDE_DIRECTORY
        "${CMAKE_SOURCE_DIR}/External/Imath/src/Imath;${CMAKE_SOURCE_DIR}/External/Imath/src;${DX12_RENDERER_IMATH_GENERATED_INCLUDE_DIRECTORY}")
set(DX12_RENDERER_EXTERNAL_OPENEXR_INCLUDE_DIRECTORY
        "${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib;${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/OpenEXR;${DX12_RENDERER_OPENEXR_GENERATED_INCLUDE_DIRECTORY};${DX12_RENDERER_EXTERNAL_IMATH_INCLUDE_DIRECTORY};${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/Iex;${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/IlmThread;${CMAKE_SOURCE_DIR}/External/OpenEXR/src/lib/OpenEXRCore")

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
        "${CMAKE_SOURCE_DIR}/External/assimp/include;${DX12_RENDERER_ASSIMP_GENERATED_INCLUDE_DIRECTORY}")
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
set(DX12_RENDERER_OIDN_GENERATED_INCLUDE_DIRECTORY "${CMAKE_BINARY_DIR}/Generated/OIDN/OpenImageDenoise")
set(OIDN_VERSION_MAJOR 2)
set(OIDN_VERSION_MINOR 5)
set(OIDN_VERSION_PATCH 1)
set(OIDN_VERSION_NUMBER 20501)
set(OIDN_VERSION_NOTE "")
set(OIDN_API_NAMESPACE "")
set(OIDN_STATIC_LIB OFF)
set(OIDN_DEVICE_CPU ON)
set(OIDN_DEVICE_SYCL OFF)
set(OIDN_DEVICE_CUDA ON)
set(OIDN_DEVICE_HIP OFF)
set(OIDN_DEVICE_METAL OFF)
set(OIDN_FILTER_RT ON)
set(OIDN_FILTER_RTLIGHTMAP OFF)
configure_file(
        "${DX12_RENDERER_OIDN_SOURCE_DIRECTORY}/include/OpenImageDenoise/config.h.in"
        "${DX12_RENDERER_OIDN_GENERATED_INCLUDE_DIRECTORY}/config.h"
        @ONLY)
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
        message(FATAL_ERROR
                "OIDN requires the tracked ISPC tool at ${DX12_RENDERER_OIDN_ISPC_DEFAULT_EXECUTABLE}.")
endif()
set(DX12_RENDERER_OIDN_ISPC_EXECUTABLE "${DX12_RENDERER_OIDN_ISPC_DEFAULT_EXECUTABLE}" CACHE FILEPATH "ISPC executable used by the isolated OIDN build.")
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
