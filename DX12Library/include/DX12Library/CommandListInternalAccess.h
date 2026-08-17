#pragma once

#include <DX12Library/CommandList.h>

#include <utility>

//Modify Begin:2026-08-17 by Hui
// This bridge is reserved for renderer infrastructure, not demo-level pass code.
class CommandListInternalAccess
{
public:
    static void FlushResourceBarriers(CommandList& commandList)
    {
        commandList.FlushResourceBarriers();
    }

    static void TrackResourceState(
        CommandList& commandList,
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        std::shared_ptr<ResourceStateRegistration> stateRegistration)
    {
        commandList.TrackResourceState(std::move(resource), std::move(stateRegistration));
    }

    static void RetireResourceState(
        CommandList& commandList,
        Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        commandList.RetireResourceState(std::move(resource));
    }

    static void RetireResource(CommandList& commandList, Resource& resource)
    {
        commandList.RetireResource(resource);
    }
};
//Modify End
