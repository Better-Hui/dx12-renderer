#pragma once

//Modify Begin:2026-07-28 by BestHui
#include <DX12Library/D3D12RenderContext.h>

#include <d3d12.h>
#include <dxgi1_6.h>

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
    ID3D12Resource* TextureFromNativeTexture(UnityTextureID texture) const;

    void RequestResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) const;
    void NotifyResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, bool uavAccess) const;

private:
    IUnityGraphicsD3D12v8* m_D3D12 = nullptr;
};
//Modify End
