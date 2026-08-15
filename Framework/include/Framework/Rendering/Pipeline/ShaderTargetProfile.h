//Modify Begin:2026-07-30 by BestHui
#pragma once

namespace ShaderTargetProfile
{
    constexpr const char* Vertex() noexcept
    {
        return "vs_6_8";
    }

    constexpr const char* Pixel() noexcept
    {
        return "ps_6_8";
    }

    constexpr const char* Compute() noexcept
    {
        return "cs_6_8";
    }

    constexpr const char* Amplification() noexcept
    {
        return "as_6_8";
    }

    constexpr const char* Mesh() noexcept
    {
        return "ms_6_8";
    }

    constexpr const char* RayTracingLibrary() noexcept
    {
        return "lib_6_8";
    }
}
//Modify End
