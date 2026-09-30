#include <Framework/Interop/UnityD3D12Interop.h>
#include <DX12Library/D3D12DeviceContext.h>

//Modify Begin:2026-09-29 by Hui
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

std::unique_ptr<ExternalCommandContext> UnityD3D12Interop::CreateExternalCommandContext() const
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

    return std::make_unique<ExternalCommandContext>(
        commandList->GetType(),
        deviceContext,
        commandList.Get());
}

bool UnityD3D12Interop::DeclareExternalResource(
    ExternalCommandContext& context,
    const ExternalCommandContext::ResourceAccess& access) const
{
    if (!IsValid() || access.Resource == nullptr)
    {
        return false;
    }
    if (access.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        // Unity's v8 state callbacks track one state per native resource. A
        // subresource-only contract would leave Unity's host tracker ambiguous.
        return false;
    }

    // Unity's v8 tracker is an optional host-side notification path. The
    // explicit InitialState remains authoritative for the renderer-side
    // ResourceStateRegistry, so a v7-only Unity integration can still use the
    // external context correctly.
    context.DeclareResource(access);
    RequestResourceState(access.Resource, access.InitialState);
    return true;
}

bool UnityD3D12Interop::DeclareExternalResources(
    ExternalCommandContext& context,
    const std::span<const ExternalCommandContext::ResourceAccess> accesses) const
{
    if (!IsValid())
    {
        return false;
    }
    for (const ExternalCommandContext::ResourceAccess& access : accesses)
    {
        if (access.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            return false;
        }
    }

    context.DeclareResources(accesses);
    for (const ExternalCommandContext::ResourceAccess& access : accesses)
    {
        RequestResourceState(access.Resource, access.InitialState);
    }
    return true;
}

void UnityD3D12Interop::EndExternalCommandContext(ExternalCommandContext& context) const
{
    Assert(!context.IsFinished(), "Cannot end an already finished Unity external command context.");
    try
    {
        context.End();
    }
    catch (...)
    {
        // End() may have recorded a best-effort restoration before rethrowing.
        // Keep Unity's host-side tracker aligned only when that restoration was
        // confirmed; never publish an invented state after restoration failed.
        if (context.WereEntryStatesRestored())
        {
            for (const ExternalCommandContext::FinalResourceState& finalState : context.GetFinalResourceStates())
            {
                NotifyResourceState(finalState.Resource, finalState.State, false);
            }
        }
        throw;
    }

    for (const ExternalCommandContext::FinalResourceState& finalState : context.GetFinalResourceStates())
    {
        NotifyResourceState(finalState.Resource, finalState.State, finalState.UavAccess);
    }
}

void UnityD3D12Interop::AbortExternalCommandContext(ExternalCommandContext& context) const
{
    Assert(!context.IsFinished(), "Cannot abort an already finished Unity external command context.");
    context.Abort();
    for (const ExternalCommandContext::FinalResourceState& finalState : context.GetFinalResourceStates())
    {
        // Abort publishes the entry state back to Unity so the host-side state
        // tracker remains aligned with the barriers recorded by the plugin.
        NotifyResourceState(finalState.Resource, finalState.State, false);
    }
}

void UnityD3D12Interop::RetireExternalCommandContext(
    std::unique_ptr<ExternalCommandContext> context) const
{
    if (context == nullptr)
    {
        return;
    }

    CollectRetiredExternalCommandContexts();

    UINT64 fenceValue = 0;
    if (m_D3D12 != nullptr && m_D3D12->GetNextFrameFenceValue != nullptr)
    {
        fenceValue = m_D3D12->GetNextFrameFenceValue();
    }

    // Unity v7 exposes a frame fence. If an older interface returns no fence,
    // keep the context until shutdown rather than releasing GPU-visible state
    // while the host command list may still be in flight.
    m_RetiredExternalContexts.push_back({ std::move(context), fenceValue });
}

void UnityD3D12Interop::CollectRetiredExternalCommandContexts() const
{
    if (m_RetiredExternalContexts.empty())
    {
        return;
    }

    if (m_D3D12 == nullptr || m_D3D12->GetFrameFence == nullptr)
    {
        return;
    }

    ID3D12Fence* const frameFence = m_D3D12->GetFrameFence();
    if (frameFence == nullptr)
    {
        return;
    }

    const UINT64 completedValue = frameFence->GetCompletedValue();
    while (!m_RetiredExternalContexts.empty())
    {
        const RetiredExternalContext& retired = m_RetiredExternalContexts.front();
        if (retired.FenceValue == 0 || retired.FenceValue > completedValue)
        {
            break;
        }
        m_RetiredExternalContexts.pop_front();
    }
}

void UnityD3D12Interop::ForgetExternalResource(ID3D12Resource* const resource) const
{
    if (resource == nullptr)
    {
        return;
    }

    const std::shared_ptr<D3D12DeviceContext> deviceContext = GetDeviceContext();
    if (deviceContext != nullptr)
    {
        deviceContext->GetResourceStateRegistry()->ForgetExternalResource(resource);
    }
}

bool UnityD3D12Interop::RecordExternalCommandRecording(
    ExternalCommandContext& context,
    const std::function<void(ID3D12GraphicsCommandList&)>& recordCommands) const
{
    if (!IsValid() || !recordCommands || context.IsFinished())
        return false;
    context.RecordNativeCommands(
        [&recordCommands](ID3D12GraphicsCommandList2& commandList)
        {
            recordCommands(commandList);
        });
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
