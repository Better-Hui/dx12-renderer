//Modify Begin:2026-07-30 by Hui
#pragma once

#include <algorithm>
#include <cstdint>

namespace RenderGraph
{
    struct RenderGraphQueueFenceValues
    {
        uint64_t Direct = 0;
        uint64_t AsyncCompute = 0;
        uint64_t Copy = 0;

        void Merge(const RenderGraphQueueFenceValues& other)
        {
            Direct = (std::max)(Direct, other.Direct);
            AsyncCompute = (std::max)(AsyncCompute, other.AsyncCompute);
            Copy = (std::max)(Copy, other.Copy);
        }

        bool IsEmpty() const
        {
            return Direct == 0 && AsyncCompute == 0 && Copy == 0;
        }
    };
}
//Modify End
