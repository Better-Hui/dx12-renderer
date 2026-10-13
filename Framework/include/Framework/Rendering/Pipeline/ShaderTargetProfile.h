//Modify Begin:2026-10-13 by Hui
#pragma once

#ifndef DX12_RENDERER_SHADER_MODEL_SUFFIX
#define DX12_RENDERER_SHADER_MODEL_SUFFIX "6_8"
#endif

namespace ShaderTargetProfile
{
    constexpr const char* Vertex() noexcept
    {
        return "vs_" DX12_RENDERER_SHADER_MODEL_SUFFIX;
    }

    constexpr const char* Pixel() noexcept
    {
        return "ps_" DX12_RENDERER_SHADER_MODEL_SUFFIX;
    }

    constexpr const char* Compute() noexcept
    {
        return "cs_" DX12_RENDERER_SHADER_MODEL_SUFFIX;
    }

    constexpr const char* Amplification() noexcept
    {
        return "as_" DX12_RENDERER_SHADER_MODEL_SUFFIX;
    }

    constexpr const char* Mesh() noexcept
    {
        return "ms_" DX12_RENDERER_SHADER_MODEL_SUFFIX;
    }

    constexpr const char* RayTracingLibrary() noexcept
    {
        return "lib_" DX12_RENDERER_SHADER_MODEL_SUFFIX;
    }
}
//Modify End
