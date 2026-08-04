//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <d3d12.h>

#include <cstdint>
#include <vector>

#include "ResourceId.h"

namespace RenderGraph
{
    struct PassResourceTransition
    {
        ResourceId Id = 0;
        D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_COMMON;
        bool InsertUavBarrier = false;
    };

    struct PassResourceStatePlan
    {
        std::vector<PassResourceTransition> InputTransitions;
        std::vector<ResourceId> AliasingOutputs;
        std::vector<PassResourceTransition> OutputTransitions;
        std::vector<ResourceId> InitOutputs;
    };
}
//Modify End
