//Modify Begin:2026-07-30 by BestHui
#pragma once

namespace ShaderTargetProfile
{
    constexpr const char* Vertex() noexcept
    {
        return "vs_6_9";
    }

    constexpr const char* Pixel() noexcept
    {
        return "ps_6_9";
    }

    constexpr const char* Compute() noexcept
    {
        return "cs_6_9";
    }

    constexpr const char* Amplification() noexcept
    {
        return "as_6_9";
    }

    constexpr const char* Mesh() noexcept
    {
        return "ms_6_9";
    }

    constexpr const char* RayTracingLibrary() noexcept
    {
        return "lib_6_9";
    }
}
//Modify End
