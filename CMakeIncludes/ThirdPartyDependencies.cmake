set(DX12_RENDERER_THIRD_PARTY_BUILD_DIRECTORY "${CMAKE_BINARY_DIR}/ThirdParty")

# GitHub third-party sources are kept under External so a valid vcpkg checkout
# is optional for the renderer build.  These projects are intentionally added
# before Framework so their native CMake targets are available to consumers.
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
if (EXISTS "${CMAKE_SOURCE_DIR}/External/meshoptimizer/CMakeLists.txt" AND NOT TARGET meshoptimizer)
        add_subdirectory("${CMAKE_SOURCE_DIR}/External/meshoptimizer"
                "${CMAKE_BINARY_DIR}/External/meshoptimizer" EXCLUDE_FROM_ALL)
endif()
if (TARGET meshoptimizer AND NOT TARGET meshoptimizer::meshoptimizer)
        add_library(meshoptimizer::meshoptimizer ALIAS meshoptimizer)
endif()

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
if (EXISTS "${CMAKE_SOURCE_DIR}/External/assimp/CMakeLists.txt" AND NOT TARGET assimp)
        add_subdirectory("${CMAKE_SOURCE_DIR}/External/assimp"
                "${CMAKE_BINARY_DIR}/External/assimp" EXCLUDE_FROM_ALL)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DX11 OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENEXR_SUPPORT OFF CACHE BOOL "" FORCE)
if (EXISTS "${CMAKE_SOURCE_DIR}/External/DirectXTex/CMakeLists.txt" AND NOT TARGET DirectXTex)
        add_subdirectory("${CMAKE_SOURCE_DIR}/External/DirectXTex"
                "${CMAKE_BINARY_DIR}/External/DirectXTex" EXCLUDE_FROM_ALL)
endif()
if (EXISTS "${CMAKE_SOURCE_DIR}/External/DirectXMesh/CMakeLists.txt" AND NOT TARGET DirectXMesh)
        add_subdirectory("${CMAKE_SOURCE_DIR}/External/DirectXMesh"
                "${CMAKE_BINARY_DIR}/External/DirectXMesh" EXCLUDE_FROM_ALL)
endif()
if (TARGET DirectXTex AND NOT TARGET Microsoft::DirectXTex)
        add_library(Microsoft::DirectXTex ALIAS DirectXTex)
endif()
if (TARGET DirectXMesh AND NOT TARGET Microsoft::DirectXMesh)
        add_library(Microsoft::DirectXMesh ALIAS DirectXMesh)
endif()

if (TARGET assimp AND NOT TARGET assimp::assimp)
        add_library(assimp::assimp ALIAS assimp)
endif()

set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
if (EXISTS "${CMAKE_SOURCE_DIR}/External/oneTBB/CMakeLists.txt" AND NOT TARGET tbb)
        add_subdirectory("${CMAKE_SOURCE_DIR}/External/oneTBB"
                "${CMAKE_BINARY_DIR}/External/oneTBB" EXCLUDE_FROM_ALL)
endif()

set(IMATH_INSTALL ON CACHE BOOL "" FORCE)
set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(PYTHON OFF CACHE BOOL "" FORCE)
set(PYBIND11 OFF CACHE BOOL "" FORCE)
if (EXISTS "${CMAKE_SOURCE_DIR}/External/Imath/CMakeLists.txt" AND NOT TARGET Imath)
        add_subdirectory("${CMAKE_SOURCE_DIR}/External/Imath"
                "${CMAKE_BINARY_DIR}/External/Imath" EXCLUDE_FROM_ALL)
endif()
if (TARGET Imath::Imath)
        set(Imath_DIR "${CMAKE_BINARY_DIR}/External/Imath/config" CACHE PATH "" FORCE)
        set(Imath_FOUND TRUE)
endif()

set(OPENEXR_FORCE_INTERNAL_IMATH ON CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON_LIBS OFF CACHE BOOL "" FORCE)
if (EXISTS "${CMAKE_SOURCE_DIR}/External/OpenEXR/CMakeLists.txt" AND NOT TARGET OpenEXR)
        add_subdirectory("${CMAKE_SOURCE_DIR}/External/OpenEXR"
                "${CMAKE_BINARY_DIR}/External/OpenEXR" EXCLUDE_FROM_ALL)
endif()
# OpenEXR main currently uses the older Imath header namespace macro names,
# while the checked-out Imath main exposes the shorter *_NS_* forms.  Define
# the compatibility aliases on OpenEXR's own targets instead of modifying the
# vendored sources, so the two GitHub snapshots remain auditable and replaceable.
foreach(DX12_RENDERER_OPENEXR_TARGET OpenEXR OpenEXRCore Iex IlmThread)
        if (TARGET ${DX12_RENDERER_OPENEXR_TARGET})
                target_compile_definitions(${DX12_RENDERER_OPENEXR_TARGET}
                        PRIVATE
                        IMATH_INTERNAL_NAMESPACE_HEADER_ENTER=IMATH_INTERNAL_NS_ENTER
                        IMATH_INTERNAL_NAMESPACE_HEADER_EXIT=IMATH_INTERNAL_NS_EXIT)
        endif()
endforeach()
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
