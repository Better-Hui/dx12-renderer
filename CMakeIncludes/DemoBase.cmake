# DemoMain.h supplies wWinMain, so use CMake's Windows-executable model instead of conflicting linker flags.
add_executable(${TARGET_NAME} WIN32 ${HEADER_FILES} ${SOURCE_FILES} ${SHADER_FILES} ${SHADERS_HEADER_FILES})

include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBasePost.cmake)

include(${CMAKE_SOURCE_DIR}/CMakeIncludes/CopyDLLs.cmake)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/CopyAssets.cmake)

# Includes and libraries
target_include_directories(${TARGET_NAME}
        PUBLIC ${EXT_HEADER_FILES}  # headers of external libraries
        PUBLIC "${CMAKE_SOURCE_DIR}/Demos/Common/include"
        PUBLIC "${CMAKE_SOURCE_DIR}/WinPixEventRuntime/Include/WinPixEventRuntime/"
        )
