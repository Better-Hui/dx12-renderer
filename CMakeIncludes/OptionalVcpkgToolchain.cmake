# Keep the preset usable on machines that do not have vcpkg installed.
# When VCPKG_ROOT points to a valid checkout, forward to its official toolchain.
if (DEFINED DX12_RENDERER_OPTIONAL_VCPKG_TOOLCHAIN_INCLUDED)
    return()
endif()
set(DX12_RENDERER_OPTIONAL_VCPKG_TOOLCHAIN_INCLUDED TRUE CACHE INTERNAL "")

if (DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" DX12_RENDERER_VCPKG_ROOT)
    set(DX12_RENDERER_VCPKG_TOOLCHAIN
            "${DX12_RENDERER_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    if (EXISTS "${DX12_RENDERER_VCPKG_TOOLCHAIN}")
        include("${DX12_RENDERER_VCPKG_TOOLCHAIN}")
        message(STATUS "Using vcpkg toolchain: ${DX12_RENDERER_VCPKG_TOOLCHAIN}")
    else()
        message(WARNING
                "VCPKG_ROOT is set but does not contain scripts/buildsystems/vcpkg.cmake; "
                "continuing without vcpkg.")
    endif()
endif()
