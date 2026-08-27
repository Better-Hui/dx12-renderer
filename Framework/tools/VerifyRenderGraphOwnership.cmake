cmake_minimum_required(VERSION 3.22)

if (NOT DEFINED REPOSITORY_ROOT)
    message(FATAL_ERROR "REPOSITORY_ROOT is required.")
endif()

cmake_path(NORMAL_PATH REPOSITORY_ROOT)

set(FIRST_PARTY_SOURCE_ROOTS
    "DX12Library/include"
    "DX12Library/src"
    "RenderGraph/include"
    "RenderGraph/src"
    "Framework/include"
    "Framework/src"
    "Demos")

set(POLICY_SOURCE_FILES)
foreach(source_root IN LISTS FIRST_PARTY_SOURCE_ROOTS)
    file(GLOB_RECURSE source_files
        LIST_DIRECTORIES FALSE
        "${REPOSITORY_ROOT}/${source_root}/*.h"
        "${REPOSITORY_ROOT}/${source_root}/*.hpp"
        "${REPOSITORY_ROOT}/${source_root}/*.inl"
        "${REPOSITORY_ROOT}/${source_root}/*.cpp"
        "${REPOSITORY_ROOT}/${source_root}/*.cc"
        "${REPOSITORY_ROOT}/${source_root}/*.cxx")
    list(APPEND POLICY_SOURCE_FILES ${source_files})
endforeach()
list(REMOVE_DUPLICATES POLICY_SOURCE_FILES)

set(BARRIER_BRIDGE_ALLOWLIST
    "DX12Library/src/CommandListInternalAccess.cpp"
    # Initializes Cubemap-owned render targets before they enter a RenderGraph.
    "DX12Library/src/Cubemap.cpp"
    "DX12Library/src/GpuReadbackBuffer.cpp"
    "DX12Library/src/GpuReadbackTexture.cpp"
    "DX12Library/src/MipGenerator.cpp"
    "DX12Library/src/ResourceUploader.cpp"
    "DX12Library/src/Window.cpp"
    "RenderGraph/src/RenderGraphCommandExecutor.cpp"
    "RenderGraph/src/RenderGraphRoot.cpp"
    "Framework/src/Rendering/Pipeline/SharedUploadBuffer.cpp"
    "Framework/src/Rendering/RayTracing/RayTracingAccelerationStructure.cpp")

set(LOW_LEVEL_BARRIER_ENCODER_ALLOWLIST
    "DX12Library/src/CommandListInternalAccess.cpp"
    "DX12Library/src/ResourceStateTracker.cpp")

set(violations)
set(checked_source_count 0)
foreach(source_file IN LISTS POLICY_SOURCE_FILES)
    file(RELATIVE_PATH relative_file "${REPOSITORY_ROOT}" "${source_file}")
    string(REPLACE "\\" "/" relative_file "${relative_file}")
    if (relative_file MATCHES "(^|/)Generated(/|$)")
        continue()
    endif()

    math(EXPR checked_source_count "${checked_source_count} + 1")
    file(READ "${source_file}" source_text)

    if (source_text MATCHES "([.]|->)(TransitionBarrier|UavBarrier|AliasingBarrier|AliasingBarrierBeforeFirstUse)[ \t\r\n]*\\(")
        list(APPEND violations "${relative_file}: direct CommandList barrier call")
    endif()

    if (source_text MATCHES "([.]|->)ResourceBarrier[ \t\r\n]*\\(")
        list(FIND LOW_LEVEL_BARRIER_ENCODER_ALLOWLIST "${relative_file}" encoder_allowlist_index)
        if (encoder_allowlist_index EQUAL -1)
            list(APPEND violations "${relative_file}: raw resource-barrier encoding outside the low-level encoder")
        endif()
    endif()

    if (source_text MATCHES "([.]|->)(TransitionResource|UavBarrier|AliasBarrier|QueueAliasingBarrier)[ \t\r\n]*\\(")
        list(FIND LOW_LEVEL_BARRIER_ENCODER_ALLOWLIST "${relative_file}" tracker_allowlist_index)
        if (tracker_allowlist_index EQUAL -1)
            list(APPEND violations "${relative_file}: ResourceStateTracker barrier API used outside the low-level encoder")
        endif()
    endif()

    if (source_text MATCHES "CommandListInternalAccess::(TransitionBarrier|UavBarrier|AliasingBarrier|AliasingBarrierBeforeFirstUse)")
        list(FIND BARRIER_BRIDGE_ALLOWLIST "${relative_file}" bridge_allowlist_index)
        if (bridge_allowlist_index EQUAL -1)
            list(APPEND violations "${relative_file}: renderer-internal barrier bridge used outside the boundary allowlist")
        endif()
    endif()

    if (relative_file MATCHES "^Demos/" AND source_text MATCHES "CommandListInternalAccess[.]h")
        list(APPEND violations "${relative_file}: Demo includes renderer-internal CommandList access")
    endif()

    if (relative_file MATCHES "^(Demos|Framework|RenderGraph)/" AND
        source_text MATCHES "ResourceStateTracker[.]h")
        list(APPEND violations "${relative_file}: upper layer includes the low-level resource-state tracker")
    endif()

    if ((relative_file STREQUAL "DX12Library/include/DX12Library/CommandList.h" OR
         relative_file STREQUAL "Framework/include/Framework/Rendering/Pipeline/CommandContext.h") AND
        source_text MATCHES "(TransitionBarrier|UavBarrier|AliasingBarrier|AliasingBarrierBeforeFirstUse)")
        list(APPEND violations "${relative_file}: public command API exposes barrier recording")
    endif()

    if (source_text MATCHES "(AreAutoBarriersEnabled|SetAutoBarriersEnabled|m_AutoBarriersEnabled|AutoTransition)")
        list(APPEND violations "${relative_file}: removed automatic-barrier mechanism was reintroduced")
    endif()

    if (source_text MATCHES "(UseAsyncComputeWhenSupported|UseCopyQueue)")
        list(APPEND violations "${relative_file}: legacy per-pass queue selection API was reintroduced")
    endif()

    if ((relative_file MATCHES "^Framework/include/Framework/Rendering/Pipeline/(CommandContext|ComputeShader|PipelineDescriptorSet)" OR
         relative_file MATCHES "^Framework/src/Rendering/Pipeline/(CommandContext|ComputeShader|PipelineDescriptorSet)") AND
        source_text MATCHES "D3D12_RESOURCE_STATES")
        list(APPEND violations "${relative_file}: descriptor binding API carries a resource-state policy")
    endif()

    if (relative_file MATCHES "^Framework/" AND
        (source_text MATCHES "RenderGraphBuilder[ \t\r\n]*[*]" OR
         source_text MATCHES "RenderGraphBuilder[ \t\r\n]*&[ \t\r\n]*m_[A-Za-z0-9_]*" OR
         source_text MATCHES "RenderGraphBuilder[ \t\r\n]+m_[A-Za-z0-9_]*" OR
         source_text MATCHES "(unique_ptr|shared_ptr)[ \t\r\n]*<[ \t\r\n]*(RenderGraph::)?RenderGraphBuilder"))
        list(APPEND violations "${relative_file}: Framework stores or points to RenderGraphBuilder")
    endif()
endforeach()

foreach(allowlisted_file IN LISTS BARRIER_BRIDGE_ALLOWLIST LOW_LEVEL_BARRIER_ENCODER_ALLOWLIST)
    if (NOT EXISTS "${REPOSITORY_ROOT}/${allowlisted_file}")
        list(APPEND violations "${allowlisted_file}: ownership allowlist entry does not exist")
    endif()
endforeach()

set(RENDER_GRAPH_ROOT_HEADER "${REPOSITORY_ROOT}/RenderGraph/include/RenderGraph/RenderGraphRoot.h")
file(READ "${RENDER_GRAPH_ROOT_HEADER}" render_graph_root_api)
if (render_graph_root_api MATCHES "(CopyTexture|DrawToTexture|DrawToGraphOutput)[ \\t\\r\\n]*\\(")
    list(APPEND violations "RenderGraph/include/RenderGraph/RenderGraphRoot.h: graph-internal work must be declared as a pass, not exposed as an external root API")
endif()

if (violations)
    list(REMOVE_DUPLICATES violations)
    list(JOIN violations "\n  - " formatted_violations)
    message(FATAL_ERROR
        "RenderGraph ownership policy failed:\n"
        "  - ${formatted_violations}\n"
        "Ordinary Framework algorithms and Demos must declare pass resource access through RenderGraphBuilder. "
        "Only the exact renderer/system boundaries in this file may encode barriers.")
endif()

list(LENGTH BARRIER_BRIDGE_ALLOWLIST bridge_boundary_count)
message(STATUS
    "RenderGraph ownership policy passed "
    "(${checked_source_count} first-party source files, ${bridge_boundary_count} barrier boundaries).")
