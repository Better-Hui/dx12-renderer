cmake_minimum_required(VERSION 3.8.0)

project("FrameworkRenderFeatures")

# Modify Begin:2026-07-30 by BestHui
set(TARGET_NAME FrameworkRenderFeatures)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include" PREFIX "include" FILES ${HEADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${SOURCE_FILES})

add_library(${TARGET_NAME} STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_compile_options(${TARGET_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBasePost.cmake)

target_link_libraries(${TARGET_NAME}
        PUBLIC Framework
        PUBLIC RenderGraph
        )
# Modify End
