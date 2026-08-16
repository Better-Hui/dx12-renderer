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
# Modify Begin:2026-07-30 by BestHui
if (NOT DEFINED DX12_RENDERER_SHADER_MODEL_VERSION)
    set(DX12_RENDERER_SHADER_MODEL_VERSION "6.9")
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

# Modify Begin:2026-07-30 by BestHui
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
set(DX12_RENDERER_SHADER_INCLUDE_ARGS
        -I "${CMAKE_SOURCE_DIR}/Shaders"
        -I "${CMAKE_SOURCE_DIR}/Framework/shaders"
        -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        )
set(DX12_RENDERER_GENERATED_SHADER_OUTPUTS)

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

    add_custom_command(
            OUTPUT "${shader_output_path}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_output_directory}"
            COMMAND "${DX12_RENDERER_DXC_EXECUTABLE}" -E main -T "${target_profile}" -HV 2021 -WX ${DX12_RENDERER_SHADER_INCLUDE_ARGS} ${shader_output_args} "${shader_absolute_path}"
            DEPENDS "${shader_absolute_path}"
            COMMENT "DXC ${target_profile}: ${shader_absolute_path}"
            VERBATIM
            )
    list(APPEND DX12_RENDERER_GENERATED_SHADER_OUTPUTS "${shader_output_path}")
    set(DX12_RENDERER_GENERATED_SHADER_OUTPUTS "${DX12_RENDERER_GENERATED_SHADER_OUTPUTS}" PARENT_SCOPE)
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

# Modify End
