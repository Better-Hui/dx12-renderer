# Enable multi-threaded builds
if (MSVC)
    add_compile_options(/MP)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()


# Source and header files
if ("${HEADER_FILES}" STREQUAL "")
    FILE(GLOB_RECURSE HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
endif ()

if ("${SOURCE_FILES}" STREQUAL "")
    FILE(GLOB_RECURSE SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
endif ()

# Shaders
# Modify Begin:2026-07-30 by Hui
if (NOT DEFINED DX12_RENDERER_SHADER_MODEL_VERSION)
    set(DX12_RENDERER_SHADER_MODEL_VERSION "6.8")
endif ()
string(REPLACE "." "_" DX12_RENDERER_SHADER_MODEL_SUFFIX "${DX12_RENDERER_SHADER_MODEL_VERSION}")
# Modify End

if ("${SHADER_FILES_VERTEX}" STREQUAL "")
    FILE(GLOB_RECURSE SHADER_FILES_VERTEX ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*_VS.hlsl)
endif ()

if ("${SHADER_FILES_PIXEL}" STREQUAL "")
    FILE(GLOB_RECURSE SHADER_FILES_PIXEL ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*_PS.hlsl)
endif ()

if ("${SHADER_FILES_COMPUTE}" STREQUAL "")
    FILE(GLOB_RECURSE SHADER_FILES_COMPUTE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*_CS.hlsl)
endif ()

list(APPEND SHADER_FILES ${SHADER_FILES_VERTEX} ${SHADER_FILES_PIXEL} ${SHADER_FILES_COMPUTE})

source_group("Resources\\Shaders" FILES ${SHADER_FILES})

# Modify Begin:2026-07-30 by Hui
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
set(DX12_RENDERER_SHADER_INCLUDE_ARGS
        -I "${CMAKE_SOURCE_DIR}/Shaders"
        -I "${CMAKE_SOURCE_DIR}/Framework/shaders"
        -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        )
# Modify Begin:2026-08-16 by Hui
# Keep shader compilation out of the Visual Studio file tree. CMake otherwise
# materializes one hashed .rule file for every shader output under CMakeFiles.
set(DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${TARGET_NAME}_CompileShaders.cmake")
set(DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT_CONTENT "cmake_minimum_required(VERSION 3.8.0)\n")
set(DX12_RENDERER_HAS_SHADER_ASSETS FALSE)

function(dx12_renderer_add_shader_compile source_path target_profile)
    get_filename_component(shader_absolute_path "${source_path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(shader_file_name "${shader_absolute_path}" NAME)
    string(REGEX REPLACE "\\.hlsl$" "" shader_name "${shader_file_name}")
    if (SHADERS_OUTPUT_HEADERS)
        set(shader_output_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders/${TARGET_NAME}")
        set(shader_output_path "${shader_output_directory}/${shader_name}.h")
        set(shader_output_args -Fh "${shader_output_path}" -Vn "ShaderBytecode_${shader_name}")
    else()
        set(shader_output_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
        set(shader_output_path "${shader_output_directory}/${shader_name}.cso")
        set(shader_output_args -Fo "${shader_output_path}")
    endif()

    set(shader_script_include_arguments)
    foreach(shader_include_argument IN LISTS DX12_RENDERER_SHADER_INCLUDE_ARGS)
        string(APPEND shader_script_include_arguments " \"${shader_include_argument}\"")
    endforeach()
    set(shader_script_output_arguments)
    foreach(shader_output_argument IN LISTS shader_output_args)
        string(APPEND shader_script_output_arguments " \"${shader_output_argument}\"")
    endforeach()
    string(APPEND DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT_CONTENT
            "if (NOT EXISTS \"${shader_output_path}\" OR \"${shader_absolute_path}\" IS_NEWER_THAN \"${shader_output_path}\")\n"
            "    message(STATUS \"DXC ${target_profile}: ${shader_file_name}\")\n"
            "    file(MAKE_DIRECTORY \"${shader_output_directory}\")\n"
            "    execute_process(\n"
            "            COMMAND \"${DX12_RENDERER_DXC_EXECUTABLE}\" -E main -T \"${target_profile}\" -HV 2021 -WX${shader_script_include_arguments}${shader_script_output_arguments} \"${shader_absolute_path}\"\n"
            "            RESULT_VARIABLE DX12_RENDERER_SHADER_COMPILE_RESULT)\n"
            "    if (NOT DX12_RENDERER_SHADER_COMPILE_RESULT EQUAL 0)\n"
            "        message(FATAL_ERROR \"DXC ${target_profile} failed: ${shader_absolute_path}\")\n"
            "    endif()\n"
            "endif()\n")
    set(DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT_CONTENT "${DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT_CONTENT}" PARENT_SCOPE)
    set(DX12_RENDERER_HAS_SHADER_ASSETS TRUE PARENT_SCOPE)
endfunction()

foreach(shader_file IN LISTS SHADER_FILES_VERTEX)
    dx12_renderer_add_shader_compile("${shader_file}" "vs_${DX12_RENDERER_SHADER_MODEL_SUFFIX}")
endforeach()
foreach(shader_file IN LISTS SHADER_FILES_PIXEL)
    dx12_renderer_add_shader_compile("${shader_file}" "ps_${DX12_RENDERER_SHADER_MODEL_SUFFIX}")
endforeach()
foreach(shader_file IN LISTS SHADER_FILES_COMPUTE)
    dx12_renderer_add_shader_compile("${shader_file}" "cs_${DX12_RENDERER_SHADER_MODEL_SUFFIX}")
endforeach()

if (DX12_RENDERER_HAS_SHADER_ASSETS)
    file(WRITE "${DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT}" "${DX12_RENDERER_SHADER_PRE_BUILD_SCRIPT_CONTENT}")
endif()

# Modify End
# Modify End
