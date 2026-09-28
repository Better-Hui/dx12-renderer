#include <Framework/Interop/UnityD3D12Interop.h>

//Modify Begin:2026-07-28 by Hui
bool UnityD3D12Interop::Initialize(IUnityInterfaces* unityInterfaces)
{
    if (unityInterfaces == nullptr)
    {
        m_D3D12 = nullptr;
        return false;
    }

    m_D3D12 = unityInterfaces->Get<IUnityGraphicsD3D12v7>();
    m_D3D12v8 = unityInterfaces->Get<IUnityGraphicsD3D12v8>();
    if (m_D3D12 == nullptr || m_D3D12->GetDevice == nullptr)
        return false;
    m_RenderContext.InitializeExternal(CreateExternalContext(true));
    return m_RenderContext.IsValid();
}

bool UnityD3D12Interop::IsValid() const
{
    return m_D3D12 != nullptr && m_D3D12->GetDevice != nullptr && m_RenderContext.IsValid();
}

ExternalD3D12Context UnityD3D12Interop::CreateExternalContext(const bool includeCommandQueue) const
{
    ExternalD3D12Context context;
    if (m_D3D12 == nullptr || m_D3D12->GetDevice == nullptr)
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

std::shared_ptr<D3D12DeviceContext> UnityD3D12Interop::GetDeviceContext() const
{
    return IsValid() ? m_RenderContext.GetD3D12DeviceContext() : nullptr;
}

std::unique_ptr<CommandList> UnityD3D12Interop::WrapCurrentCommandList() const
{
    if (!IsValid())
        return nullptr;
    ID3D12GraphicsCommandList* current = GetCurrentCommandList();
    const std::shared_ptr<D3D12DeviceContext> deviceContext = GetDeviceContext();
    if (current == nullptr || deviceContext == nullptr)
        return nullptr;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList;
    if (FAILED(current->QueryInterface(IID_PPV_ARGS(&commandList))))
        return nullptr;
    return std::make_unique<CommandList>(
        D3D12_COMMAND_LIST_TYPE_DIRECT, deviceContext, commandList.Get());
}

bool UnityD3D12Interop::RecordExternalCommandRecording(
    const std::function<void(ID3D12GraphicsCommandList&)>& recordCommands) const
{
    ID3D12GraphicsCommandList* commandList = GetCurrentCommandList();
    if (commandList == nullptr || !recordCommands)
        return false;
    recordCommands(*commandList);
    return true;
}

void UnityD3D12Interop::RequestResourceState(ID3D12Resource* resource, const D3D12_RESOURCE_STATES state) const
{
    if (IsValid() && m_D3D12v8 != nullptr && m_D3D12v8->RequestResourceState != nullptr && resource != nullptr)
    {
        m_D3D12v8->RequestResourceState(resource, state);
    }
}

void UnityD3D12Interop::NotifyResourceState(ID3D12Resource* resource, const D3D12_RESOURCE_STATES state, const bool uavAccess) const
{
    if (IsValid() && m_D3D12v8 != nullptr && m_D3D12v8->NotifyResourceState != nullptr && resource != nullptr)
    {
        m_D3D12v8->NotifyResourceState(resource, state, uavAccess);
    }
}
//Modify End
