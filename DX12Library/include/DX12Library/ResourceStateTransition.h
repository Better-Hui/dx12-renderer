//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <d3d12.h>

class Resource;

struct ResourceStateTransition
{
    const Resource* Target = nullptr;
    D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_COMMON;
};
//Modify End
