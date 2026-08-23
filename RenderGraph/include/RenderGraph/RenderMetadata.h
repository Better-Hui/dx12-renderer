#pragma once

#include <cstdint>

namespace RenderGraph
{
    struct RenderMetadata
    {
        uint32_t m_ScreenWidth = 1;
        uint32_t m_ScreenHeight = 1;
//Modify Begin:2026-08-07 by Hui
        uint32_t m_DisplayWidth = 1;
        uint32_t m_DisplayHeight = 1;
//Modify End
        double m_Time = 0.0;
//Modify Begin:2026-08-23 by Hui
        float m_DeltaTime = 0.0f;
//Modify End
        uint64_t m_FrameIndex = 0;
    };
}
