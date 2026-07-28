#include <Framework/UnityD3D12Interop.h>

//Modify Begin:2026-07-28 by BestHui
bool UnityD3D12Interop::Initialize(IUnityInterfaces* unityInterfaces)
{
    if (unityInterfaces == nullptr)
    {
        m_D3D12 = nullptr;
        return false;
    }

    m_D3D12 = unityInterfaces->Get<IUnityGraphicsD3D12v8>();
    return m_D3D12 != nullptr && m_D3D12->GetDevice != nullptr;
}

bool UnityD3D12Interop::IsValid() const
{
    return m_D3D12 != nullptr && m_D3D12->GetDevice != nullptr;
}

ExternalD3D12Context UnityD3D12Interop::CreateExternalContext(const bool includeCommandQueue) const
{
    ExternalD3D12Context context;
    if (!IsValid())
    {
        return context;
    }

    context.Device = m_D3D12->GetDevice();
    if (includeCommandQueue && m_D3D12->GetCommandQueue != nullptr)
    {
        context.DirectCommandQueue = m_D3D12->GetCommandQueue();
    }
    return context;
}

ID3D12Device* UnityD3D12Interop::GetDevice() const
{
    return IsValid() ? m_D3D12->GetDevice() : nullptr;
}

ID3D12CommandQueue* UnityD3D12Interop::GetCommandQueue() const
{
    return IsValid() && m_D3D12->GetCommandQueue != nullptr ? m_D3D12->GetCommandQueue() : nullptr;
}

ID3D12GraphicsCommandList* UnityD3D12Interop::GetCurrentCommandList() const
{
    if (!IsValid() || m_D3D12->CommandRecordingState == nullptr)
    {
        return nullptr;
    }

    UnityGraphicsD3D12RecordingState recordingState = {};
    return m_D3D12->CommandRecordingState(&recordingState) ? recordingState.commandList : nullptr;
}

ID3D12Resource* UnityD3D12Interop::TextureFromNativeTexture(const UnityTextureID texture) const
{
    return IsValid() && m_D3D12->TextureFromNativeTexture != nullptr
        ? m_D3D12->TextureFromNativeTexture(texture)
        : nullptr;
}

void UnityD3D12Interop::RequestResourceState(ID3D12Resource* resource, const D3D12_RESOURCE_STATES state) const
{
    if (IsValid() && m_D3D12->RequestResourceState != nullptr && resource != nullptr)
    {
        m_D3D12->RequestResourceState(resource, state);
    }
}

void UnityD3D12Interop::NotifyResourceState(ID3D12Resource* resource, const D3D12_RESOURCE_STATES state, const bool uavAccess) const
{
    if (IsValid() && m_D3D12->NotifyResourceState != nullptr && resource != nullptr)
    {
        m_D3D12->NotifyResourceState(resource, state, uavAccess);
    }
}
//Modify End
