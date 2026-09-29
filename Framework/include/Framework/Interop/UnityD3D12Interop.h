#pragma once

//Modify Begin:2026-09-29 by Hui
#include <DX12Library/D3D12RenderContext.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/ExternalCommandContext.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <functional>
#include <deque>
#include <span>

#include <IUnityGraphicsD3D12.h>
#include <IUnityInterface.h>

class UnityD3D12Interop
{
public:
    bool Initialize(IUnityInterfaces* unityInterfaces);
    bool IsValid() const;

    ExternalD3D12Context CreateExternalContext(bool includeCommandQueue) const;

    ID3D12Device* GetDevice() const;
    ID3D12CommandQueue* GetCommandQueue() const;
    ID3D12GraphicsCommandList* GetCurrentCommandList() const;
    std::shared_ptr<D3D12DeviceContext> GetDeviceContext() const;
    std::unique_ptr<ExternalCommandContext> CreateExternalCommandContext() const;
    bool DeclareExternalResource(
        ExternalCommandContext& context,
        const ExternalCommandContext::ResourceAccess& access) const;
    bool DeclareExternalResources(
        ExternalCommandContext& context,
        std::span<const ExternalCommandContext::ResourceAccess> accesses) const;
    void EndExternalCommandContext(ExternalCommandContext& context) const;
    void AbortExternalCommandContext(ExternalCommandContext& context) const;
    /**
     * Retain an ended external context until Unity's frame fence has passed.
     * This is required when the context used dynamic descriptor heaps or
     * transient upload allocations while recording Unity's command list.
     */
    void RetireExternalCommandContext(std::unique_ptr<ExternalCommandContext> context) const;
    void CollectRetiredExternalCommandContexts() const;
    void ForgetExternalResource(ID3D12Resource* resource) const;
    std::unique_ptr<CommandList> WrapCurrentCommandList() const;
    ID3D12Resource* TextureFromNativeTexture(UnityTextureID texture) const;

    bool RecordExternalCommandRecording(
        ExternalCommandContext& context,
        const std::function<void(ID3D12GraphicsCommandList&)>& recordCommands) const;

private:
    void RequestResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) const;
    void NotifyResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, bool uavAccess) const;

    IUnityGraphicsD3D12v7* m_D3D12 = nullptr;
    IUnityGraphicsD3D12v8* m_D3D12v8 = nullptr;
    D3D12RenderContext m_RenderContext;
    struct RetiredExternalContext
    {
        std::unique_ptr<ExternalCommandContext> Context;
        UINT64 FenceValue = 0;
    };
    mutable std::deque<RetiredExternalContext> m_RetiredExternalContexts;
};
//Modify End
